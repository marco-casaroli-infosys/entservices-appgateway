/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
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
 **/

#include <string>
#include <plugins/JSONRPC.h>
#include <plugins/IShell.h>
#include "AppGatewayImplementation.h"
#include "AppGatewayTelemetry.h"
#include "UtilsLogging.h"
#include "ContextUtils.h"
#include "ObjectUtils.h"
#include "UtilsInterface.h"
#include <fstream>
#include <streambuf>
#include "UtilsCallsign.h"
#include "UtilsFirebolt.h"
#include "StringUtils.h"

#define DEFAULT_CONFIG_PATH "/etc/app-gateway/resolution.base.json"

// Telemetry client definition for job timing macros
AGW_DEFINE_TELEMETRY_CLIENT(AGW_PLUGIN_APPGATEWAY)
#define RESOLUTIONS_PATH_CFG "/etc/app-gateway/resolutions.json"

// Build and vendor config paths are defined via CMake
// These should be set in the platform-specific .bbappend file
#ifndef BUILD_CONFIG_PATH
#define BUILD_CONFIG_PATH ""
#endif

#ifndef VENDOR_CONFIG_PATH
#define VENDOR_CONFIG_PATH ""
#endif

namespace WPEFramework
{
    namespace Plugin
    {
        SERVICE_REGISTRATION(AppGatewayImplementation, 1, 0, 0);

        class RegionalResolutionConfig : public Core::JSON::Container
        {
        private:
            RegionalResolutionConfig(const RegionalResolutionConfig &) = delete;
            RegionalResolutionConfig &operator=(const RegionalResolutionConfig &) = delete;

        public:
            class Region : public Core::JSON::Container
            {
            public:
                Region()
                : Core::JSON::Container()
                {
                    Add(_T("countryCodes"), &countryCodes);
                    Add(_T("paths"), &paths);
                }

                Region(const Region& other)
                : Core::JSON::Container(),
                  countryCodes(other.countryCodes),
                  paths(other.paths)
                {
                    Add(_T("countryCodes"), &countryCodes);
                    Add(_T("paths"), &paths);
                }
                Region& operator=(const Region& other)
                {
                    if (this != &other) {
                        countryCodes = other.countryCodes;
                        paths = other.paths;
                    }
                    return *this;
                }
                ~Region() {}

                Core::JSON::ArrayType<Core::JSON::String> countryCodes;
                Core::JSON::ArrayType<Core::JSON::String> paths;

                bool HasCountryCode(const std::string& country) const {
                    auto index = countryCodes.Elements();
                    while (index.Next()) {
                        std::string code = index.Current().Value();
                        // Case-insensitive equality using StringUtils::toLower
                        if (StringUtils::toLower(code) == StringUtils::toLower(country)) {
                            return true;
                        }
                    }
                    return false;
                }

                std::vector<std::string> GetPaths() const {
                    std::vector<std::string> result;
                    auto index = paths.Elements();
                    while (index.Next()) {
                        result.push_back(index.Current().Value().c_str());
                    }
                    return result;
                }
            };

            RegionalResolutionConfig()
            : Core::JSON::Container()
            {
                Add(_T("defaultCountryCode"), &defaultCountryCode);
                Add(_T("regions"), &regions);
            }
            ~RegionalResolutionConfig() {}

            std::vector<std::string> GetPathsForCountry(const std::string& country) const {
                std::vector<std::string> result;

                // Search through regions for matching country code
                auto index = regions.Elements();
                while (index.Next()) {
                    const Region& region = index.Current();
                    if (region.HasCountryCode(country)) {
                        result = region.GetPaths();
                        LOGINFO("Found %zu paths for country '%s'", result.size(), country.c_str());
                        return result;
                    }
                }

                // If no match found and we have a default country, try that
                if (!country.empty() && defaultCountryCode.IsSet()) {
                    std::string defaultCode = defaultCountryCode.Value();
                    if (!defaultCode.empty() && StringUtils::toLower(country) != StringUtils::toLower(defaultCode)) {
                        LOGWARN("Country '%s' not found, trying default country '%s'",
                                country.c_str(), defaultCode.c_str());
                        return GetPathsForCountry(defaultCode);
                    }
                }

                return result;
            }

        public:
            Core::JSON::String defaultCountryCode;
            Core::JSON::ArrayType<Region> regions;
        };

        AppGatewayImplementation::AppGatewayImplementation()
            : mService(nullptr),
            mResolverPtr(nullptr), 
            mAppNotifications(nullptr),
            mAppGatewayResponder(nullptr),
            mInternalGatewayResponder(nullptr)
        {
            LOGINFO("AppGatewayImplementation constructor");
        }

        AppGatewayImplementation::~AppGatewayImplementation()
        {
            LOGINFO("AppGatewayImplementation destructor");
            if (nullptr != mService)
            {
                mService->Release();
                mService = nullptr;
            }

            if (nullptr != mAppNotifications)
            {
                mAppNotifications->Release();
                mAppNotifications = nullptr;
            }

            if (nullptr != mInternalGatewayResponder)
            {
                mInternalGatewayResponder->Release();
                mInternalGatewayResponder = nullptr;
            }

            if (nullptr != mAppGatewayResponder)
            {
                mAppGatewayResponder->Release();
                mAppGatewayResponder = nullptr;
            }

            // Shared pointer will automatically clean up
            mResolverPtr.reset();
        }

        uint32_t AppGatewayImplementation::Configure(PluginHost::IShell *shell)
        {
            LOGINFO("Configuring AppGateway");
            ASSERT(shell != nullptr);
            mService = shell;
            mService->AddRef();

            return InitializeResolver();
        }
        
        uint32_t AppGatewayImplementation::InitializeResolver() {
            // Initialize resolver after setting mService
            try {
                mResolverPtr = std::make_shared<Resolver>(mService);
            } catch (const std::bad_alloc& e) {
                LOGERR("Failed to create Resolver instance: %s", e.what());
                return Core::ERROR_GENERAL;
            }

            // Read country from build config
            std::string country = ReadCountryFromConfigFile();
            if (country.empty()) {
                LOGWARN("No country found in build config, will use default from resolutions config");
            } else {
                LOGINFO("Device country code: %s", country.c_str());
            }

            // Load the regional resolutions configuration
            RegionalResolutionConfig regionalConfig;
            Core::OptionalType<Core::JSON::Error> error;
            std::ifstream resolutionConfigFile(RESOLUTIONS_PATH_CFG);

            if (!resolutionConfigFile.is_open())
            {
                LOGWARN("Failed to open resolutions config file: %s, falling back to default config", RESOLUTIONS_PATH_CFG);

                // Fallback: Load only the base resolution file
                std::vector<std::string> fallbackPaths = {DEFAULT_CONFIG_PATH};
                LOGINFO("Using fallback: loading default config path: %s", DEFAULT_CONFIG_PATH);

                Core::hresult configResult = InternalResolutionConfigure(std::move(fallbackPaths));
                if (configResult != Core::ERROR_NONE) {
                    LOGERR("Failed to configure resolutions from fallback path");
                    return configResult;
                }
                return Core::ERROR_NONE;
            }

            // Parse the regional config file
            std::string configContent((std::istreambuf_iterator<char>(resolutionConfigFile)), std::istreambuf_iterator<char>());
            resolutionConfigFile.close();

            if (regionalConfig.FromString(configContent, error) == false)
            {
                LOGERR("Failed to parse regional resolutions config file, error: '%s'",
                       (error.IsSet() ? error.Value().Message().c_str() : "Unknown"));
                LOGWARN("Falling back to default config path: %s", DEFAULT_CONFIG_PATH);
                std::vector<std::string> fallbackPaths = { DEFAULT_CONFIG_PATH };
                Core::hresult configResult = InternalResolutionConfigure(std::move(fallbackPaths));
                if (configResult != Core::ERROR_NONE) {
                    LOGERR("Failed to configure resolutions from fallback path after parse error");
                    return configResult;
                }
                return Core::ERROR_NONE;
            }

            // If country is empty, use the default from config
            if (country.empty() && regionalConfig.defaultCountryCode.IsSet()) {
                country = regionalConfig.defaultCountryCode.Value();
                LOGINFO("Using default country code from config: %s", country.c_str());
            }

            // Get paths for the country
            std::vector<std::string> configPaths = regionalConfig.GetPathsForCountry(country);

            if (configPaths.empty()) {
                LOGERR("No configuration paths found for country '%s' and no fallback available", country.c_str());

                // Last resort fallback
                configPaths = {DEFAULT_CONFIG_PATH};
                LOGWARN("Using last resort fallback: %s", DEFAULT_CONFIG_PATH);
            }

            LOGINFO("Loading %zu configuration paths for country '%s'", configPaths.size(), country.c_str());
            Core::hresult configResult = InternalResolutionConfigure(std::move(configPaths));
            if (configResult != Core::ERROR_NONE) {
                LOGERR("Failed to configure resolutions from country-specific paths");
                return configResult;
            }

            return Core::ERROR_NONE;
        }

        Core::hresult AppGatewayImplementation::Configure(Exchange::IAppGatewayResolver::IStringIterator *const &paths)
        {
            LOGINFO("Call AppGatewayImplementation::Configure");

            if (paths == nullptr)
            {
                LOGERR("Configure called with null paths iterator");
                return Core::ERROR_BAD_REQUEST;
            }

            if (mResolverPtr == nullptr)
            {
                LOGERR("Resolver not initialized");
                return Core::ERROR_GENERAL;
            }

            // Clear existing resolutions before loading new configuration
            // mResolverPtr->ClearResolutions();

            std::vector<std::string> configPaths;

            // Collect all paths first
            std::string currentPath;
            while (paths->Next(currentPath) == true)
            {
                configPaths.push_back(currentPath);
                LOGINFO("Found config path: %s", currentPath.c_str());
            }

            if (configPaths.empty())
            {
                LOGERR("No valid configuration paths provided");
                return Core::ERROR_BAD_REQUEST;
            }

            LOGINFO("Processing %zu configuration paths in override order", configPaths.size());
            return InternalResolutionConfigure(std::move(configPaths));
            
        }

        Core::hresult AppGatewayImplementation::InternalResolutionConfigure(std::vector<std::string>&& configPaths){
            // Process all paths in order - later paths override earlier ones
            bool anyConfigLoaded = false;
            for (size_t i = 0; i < configPaths.size(); i++)
            {
                const std::string &configPath = configPaths[i];
                LOGINFO("Processing config path %zu/%zu: %s", i + 1, configPaths.size(), configPath.c_str());

                if (mResolverPtr->LoadConfig(configPath))
                {
                    LOGINFO("Successfully loaded configuration from: %s", configPath.c_str());
                    anyConfigLoaded = true;
                }
                else
                {
                    LOGERR("Failed to load configuration from: %s", configPath.c_str());
                    // Continue processing other paths instead of failing completely
                }
            }

            if (!anyConfigLoaded)
            {
                LOGERR("Failed to load configuration from any provided path");
                return Core::ERROR_GENERAL;
            }

            LOGINFO("Configuration complete. Final resolutions loaded with override priority (later paths take precedence)");
            return Core::ERROR_NONE;

        }

        Core::hresult AppGatewayImplementation::Resolve(const Context& context, const string& origin, const string& method, const string& params, string& resolution)
        {
            LOGTRACE("method=%s params=%s", method.c_str(), params.c_str());
            return InternalResolve(context, method, params, origin, resolution);
        }

        Core::hresult AppGatewayImplementation::InternalResolve(const Context& context, const string& method, const string& params, const string& origin, string& resolution)
        {
            Core::hresult result = FetchResolvedData(context, method, params, origin, resolution);
            if (!resolution.empty()) {
                LOGTRACE("Final resolution: %s", resolution.c_str());
                Core::IWorkerPool::Instance().Submit(RespondJob::Create(this, context, resolution, origin));
            }
            return result;
        }

        Core::hresult AppGatewayImplementation::FetchResolvedData(const Context &context, const string &method, const string &params, const string &origin, string& resolution) {
            JsonObject params_obj;
            Core::hresult result = Core::ERROR_NONE;
            if (mResolverPtr == nullptr)
            {
                LOGERR("Resolver not initialized");
                ErrorUtils::CustomInitialize("Resolver not initialized", resolution);
                return Core::ERROR_GENERAL;
            }

            // Check if resolver has any resolutions loaded
            if (!mResolverPtr->IsConfigured())
            {
                LOGERR("Resolver not configured - no resolutions loaded. Call Configure() first.");
                ErrorUtils::CustomInitialize("Resolver not configured", resolution);
                return Core::ERROR_GENERAL;
            }
            // Resolve the alias from the method
            std::string alias = mResolverPtr->ResolveAlias(method);

            if (alias.empty())
            {
                LOGERR("No alias found for method: %s", method.c_str());
                ErrorUtils::NotSupported(resolution);
                return Core::ERROR_GENERAL;
            }

            std::string permissionGroup;
            if (mResolverPtr->HasPermissionGroup(method, permissionGroup)) {
                LOGTRACE("Method '%s' requires permission group '%s'", method.c_str(), permissionGroup.c_str());
                ScopedInterface<Exchange::IAppGatewayAuthenticator> authenticator(
                    mService,
                    GATEWAY_AUTHENTICATOR_CALLSIGN);

                if (authenticator) {
                    bool allowed = false;
                    if (Core::ERROR_NONE != authenticator->CheckPermissionGroup(context.appId, permissionGroup, allowed)) {
                        LOGERR("Failed to check permission group '%s' for appId '%s'", permissionGroup.c_str(), context.appId.c_str());
                        // Track external service error - Permission service failure
                        AppGatewayTelemetry::getInstance().RecordExternalServiceErrorInternal(context, AGW_SERVICE_PERMISSION);
                        ErrorUtils::NotPermitted(resolution);
                        return Core::ERROR_GENERAL;
                    }
                    if (!allowed) {
                        LOGERR("AppId '%s' not allowed in permission group '%s'", context.appId.c_str(), permissionGroup.c_str());
                        ErrorUtils::NotPermitted(resolution);
                        return Core::ERROR_GENERAL;
                    }
                } else {
                    // Track external service error - Permission service unavailable
                    AppGatewayTelemetry::getInstance().RecordExternalServiceErrorInternal(context, AGW_SERVICE_PERMISSION);
                    ErrorUtils::NotPermitted(resolution);
                    return Core::ERROR_GENERAL;
                }
            }
            LOGTRACE("Resolved method '%s' to alias '%s'", method.c_str(), alias.c_str());            
            // Check if the given method is an event
            if (mResolverPtr->HasEvent(method)) {
                result = PreProcessEvent(context, alias, method, origin, params, resolution);
            } else if(mResolverPtr->HasComRpcRequestSupport(method)) {
                result = ProcessComRpcRequest(context, alias, method, params, origin, resolution);
            } else {
                // Check if includeContext is enabled for this method
                std::string finalParams = UpdateContext(context, method, params, origin);
                LOGTRACE("Final Request params alias=%s Params = %s", alias.c_str(), finalParams.c_str());

                result = mResolverPtr->CallThunderPlugin(alias, finalParams, resolution);
                if (result != Core::ERROR_NONE) {
                    LOGERR("Failed to retrieve resolution from Thunder method %s", alias.c_str());
                    ErrorUtils::CustomInternal("Failed with internal error", resolution);
                } else {
                    if (resolution.empty()) {
                        resolution = "null";
                    }
                }
            }
            return result;
        }

        string AppGatewayImplementation::UpdateContext(const Context &context, const string& method, const string& params, const string& origin, const bool& onlyAdditionalContext) {
            // Check if includeContext is enabled for this method
            std::string finalParams = params;
            JsonValue additionalContext;
            if (mResolverPtr->HasIncludeContext(method, additionalContext)) {
                LOGTRACE("Method '%s' requires context inclusion", method.c_str());
                JsonObject paramsObj;
                if (!paramsObj.FromString(params))
                {
                    // In json rpc params are optional
                    LOGWARN("Failed to parse original params as JSON: %s", params.c_str());
                }
                if (onlyAdditionalContext) {
                    if (additionalContext.Content() == WPEFramework::Core::JSON::Variant::type::OBJECT) {
                        JsonObject contextWithOrigin = additionalContext.Object();
                        contextWithOrigin["origin"] = origin;
                        JsonObject finalParamsObject;
                        finalParamsObject["params"] = paramsObj;
                        finalParamsObject["_additionalContext"] = contextWithOrigin;
                        finalParamsObject.ToString(finalParams);
                    } else {
                        LOGERR("Additional context is not a JSON object for method: %s", method.c_str());
                    }
                } else {
                    JsonObject contextObj;
                    contextObj["appId"] = context.appId;
                    contextObj["connectionId"] = context.connectionId;
                    contextObj["requestId"] = context.requestId;
                    paramsObj["context"] = contextObj;
                    paramsObj.ToString(finalParams);
                }                
            }
            return finalParams;
        }

        uint32_t AppGatewayImplementation::ProcessComRpcRequest(const Context &context, const string& alias, const string& method, const string& params, const string& origin, string &resolution) {
            uint32_t result = Core::ERROR_GENERAL;
            Exchange::IAppGatewayRequestHandler *requestHandler = mService->QueryInterfaceByCallsign<Exchange::IAppGatewayRequestHandler>(alias);
            if (requestHandler != nullptr) {
                std::string finalParams = UpdateContext(context, method, params, origin, true);

                if (Core::ERROR_NONE != requestHandler->HandleAppGatewayRequest(context, method, finalParams, resolution)) {
                    LOGERR("HandleAppGatewayRequest failed for callsign: %s", alias.c_str());
                    
                    // Record API error for telemetry
                    AppGatewayTelemetry::getInstance().RecordApiError(context, method);
                    
                    if (resolution.empty()){
                        ErrorUtils::CustomInternal("HandleAppGatewayRequest failed", resolution);
                    }
                } else {
                    result = Core::ERROR_NONE;
                }
                requestHandler->Release();
            } else {
                LOGERR("Bad configuration %s Not available with COM RPC", alias.c_str());
                ErrorUtils::NotAvailable(resolution);
            }

            return result;
        }
        

        uint32_t AppGatewayImplementation::PreProcessEvent(const Context &context, const string& alias, const string &method, const string& origin, const string& params,
        string &resolution) {
            JsonObject params_obj;
            if (params_obj.FromString(params)) {
                    bool resultValue;
                    // Use ObjectUtils::HasBooleanEntry and populate resultValue
                    if (ObjectUtils::HasBooleanEntry(params_obj, "listen", resultValue)) {
                        LOGTRACE("Event method '%s' with listen: %s", method.c_str(), resultValue ? "true" : "false");
                        string eventName = method;
                        if (mResolverPtr->IsVersionedEvent(method)) {
                            eventName = ContextUtils::GetEventNameFromContextBasedonVersion(context.version, method);
                        }
                        
                        auto ret_value = HandleEvent(context, alias, eventName, origin, resultValue);
                        if (ret_value == Core::ERROR_NONE && resultValue) {
                            std::string hookMethod;
                            if (mResolverPtr->HasEventHook(method, hookMethod)) {
                                LOGINFO("PreProcessEvent: eventHook detected for method=%s hook=%s appId=%s",
                                         method.c_str(), hookMethod.c_str(), context.appId.c_str());
                                Core::IWorkerPool::Instance().Submit(
                                    EventHookJob::Create(this, context, hookMethod));
                            }
                        }
                        JsonObject returnResult;
                        returnResult["listening"] = resultValue;
                        returnResult["event"] = method;
                        returnResult.ToString(resolution);
                        return ret_value;
                    } else {
                        LOGERR("Event method '%s' missing required boolean 'listen' parameter", method.c_str());
                        ErrorUtils::CustomBadRequest("Missing required boolean 'listen' parameter", resolution);
                        return Core::ERROR_BAD_REQUEST;
                    }
            } else {
                    LOGERR("Event method '%s' called without parameters", method.c_str());
                    ErrorUtils::CustomBadRequest("Event methods require parameters", resolution);
                    return Core::ERROR_BAD_REQUEST;
            }
        }

        void AppGatewayImplementation::EventHookJob::Dispatch()
        {
            AGW_TRACK_JOB_LATENCY(timer, "EventHookJob[" + mHookMethod + "]",
                mContext.requestId, mContext.connectionId, mContext.appId);
            LOGINFO("EventHookJob: triggering hook method=%s for appId=%s",
                     mHookMethod.c_str(), mContext.appId.c_str());
            std::string resolution;
            const auto rc = mParent.FetchResolvedData(mContext, mHookMethod, "{}", "", resolution);
            if (rc != Core::ERROR_NONE) {
                LOGERR("EventHookJob: hook method=%s failed rc=%u", mHookMethod.c_str(), rc);
            }
        }

        Core::hresult AppGatewayImplementation::HandleEvent(const Context &context, const string &alias,  const string &event, const string &origin, const bool listen) {
            Core::SafeSyncType<Core::CriticalSection> lock(mAppNotificationsLock);
            if (nullptr == mAppNotifications) {
                mAppNotifications = mService->QueryInterfaceByCallsign<Exchange::IAppNotifications>(APP_NOTIFICATIONS_CALLSIGN);
                if (nullptr == mAppNotifications) {
                    LOGERR("Failed to get IAppNotifications interface");
                    return Core::ERROR_GENERAL;
                } else {
                    LOGINFO("IAppNotifications interface acquired");
                }
            }
            return mAppNotifications->Subscribe(ContextUtils::ConvertAppGatewayToNotificationContext(context,origin), listen, alias, event);
        }

        void AppGatewayImplementation::SendToLaunchDelegate(const Context& context, const string& payload)
        {
            Core::SafeSyncType<Core::CriticalSection> lock(mInternalGatewayResponderLock);
            if (nullptr == mInternalGatewayResponder) {
                mInternalGatewayResponder = mService->QueryInterfaceByCallsign<Exchange::IAppGatewayResponder>(INTERNAL_GATEWAY_CALLSIGN);
                if (nullptr == mInternalGatewayResponder) {
                    LOGERR("Failed to get Internal Responder interface");
                    return;
                } else {
                    LOGINFO("Internal Responder interface acquired");
                }
            }
            mInternalGatewayResponder->Respond(context, payload);
        }

        // Helper: read a string key from a JSON file; returns empty if any step fails.
        static std::string ReadJsonStringKey(const std::string& filePath, const std::string& key, const char* tag) {
            if (filePath.empty()) {
                return "";
            }
            std::ifstream file(filePath);
            if (!file.is_open()) {
                LOGINFO("%s file not found: %s", tag, filePath.c_str());
                return "";
            }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            JsonObject json;
            if (!json.FromString(content)) {
                LOGERR("Failed to parse %s JSON: %s", tag, filePath.c_str());
                return "";
            }
            if (!json.HasLabel(key.c_str())) {
                LOGWARN("No '%s' field found in %s: %s", key.c_str(), tag, filePath.c_str());
                return "";
            }
            std::string value = json[key.c_str()].String();
            LOGINFO("%s '%s' read: %s", tag, key.c_str(), value.c_str());
            return value;
        }

        std::string AppGatewayImplementation::ReadCountryFromConfigFile() {
            // Both config paths empty: rely on defaultCountryCode in resolutions.json later.
            if (strlen(VENDOR_CONFIG_PATH) == 0 && strlen(BUILD_CONFIG_PATH) == 0) {
                LOGINFO("Platform config paths not set; will use defaultCountryCode from resolutions.json if present");
                return "";
            }

            // Try vendor first, then build.
            const std::string vendorPath = VENDOR_CONFIG_PATH;
            const std::string buildPath  = BUILD_CONFIG_PATH;

            std::string country = ReadJsonStringKey(vendorPath, "country", "Vendor config");
            if (!country.empty()) {
                return country;
            }
            country = ReadJsonStringKey(buildPath, "country", "Build config");
            return country; // may be empty; caller handles fallback
        }

    } // namespace Plugin
} // namespace WPEFramework

