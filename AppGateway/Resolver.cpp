/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
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

#include "Resolver.h"
#include <fstream>
#include <iostream>
#include "UtilsLogging.h"
#include "StringUtils.h"
#include "UtilsJsonrpcDirectLink.h"
#include <core/JSON.h>


namespace WPEFramework
{
    namespace Plugin
    {

        Resolver::Resolver(PluginHost::IShell *shell)
            : mService(shell), mResolutions(), mMutex()
        {
            LOGINFO("[Resolver] Constructor - configurations will be loaded via LoadConfig");
        }

        Resolver::~Resolver()
        {
            LOGINFO("Call Resolver destructor");
            if (nullptr != mService)
            {
                mService->Release();
                mService = nullptr;
            }
        }

        bool Resolver::LoadConfig(const std::string &path)
        {
            std::ifstream file(path);
            if (!file.is_open())
            {
                LOGERR("[Resolver] Failed to open config file: %s", path.c_str());
                return false;
            }

            // Use ConfigContainer for direct JSON parsing from file
            ConfigContainer config;
            WPEFramework::Core::OptionalType<WPEFramework::Core::JSON::Error> error;

            // Read file content efficiently
            std::string jsonContent((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
            file.close();

            if (!config.IElement::FromString(jsonContent, error))
            {
                LOGERR("[Resolver] Failed to parse JSON from: %s", path.c_str());
                if (error.IsSet())
                {
                    LOGERR(" - Error: %s", error.Value().Message().c_str());
                }
                return false;
            }

            // Direct access to resolutions via ConfigContainer
            if (!config.Resolutions.IsSet())
            {
                LOGERR("[Resolver] No 'resolutions' object in config file: %s", path.c_str());
                return false;
            }

            // Acquire lock before modifying mResolutions
            std::lock_guard<std::mutex> lock(mMutex);
            size_t loadedCount = 0;
            size_t overriddenCount = 0;

            // Iterate through all resolution entries with optimized parsing
            WPEFramework::Core::JSON::VariantContainer::Iterator it = config.Resolutions.Variants();
            while (it.Next())
            {
                const std::string &key = StringUtils::toLower(it.Label());
                WPEFramework::Core::JSON::Variant resolutionVariant = it.Current();

                if (resolutionVariant.IsSet() && !resolutionVariant.IsNull())
                {
                    // Create Resolution struct and populate using helper functions
                    Resolution r;
                    WPEFramework::Core::JSON::VariantContainer resolutionObj = resolutionVariant.Object();

                    // Use helper functions to extract all fields consistently
                    r.alias = ExtractStringField(resolutionObj, "alias");
                    r.event = ExtractStringField(resolutionObj, "event");
                    r.eventHook = ExtractStringField(resolutionObj, "eventHook");
                    r.permissionGroup = ExtractStringField(resolutionObj, "permissionGroup");
                    r.additionalContext = ExtractAdditionalContext(resolutionObj, "additionalContext");
                    bool hasAdditionalContext = r.additionalContext.Content() == WPEFramework::Core::JSON::Variant::type::OBJECT;
                    r.includeContext = ExtractBooleanField(resolutionObj, "includeContext", hasAdditionalContext);
                    r.useComRpc = ExtractBooleanField(resolutionObj, "useComRpc", hasAdditionalContext);
                    // Event which has different payload based on version
                    r.versionedEvent = ExtractBooleanField(resolutionObj, "versionedEvent", hasAdditionalContext);

                    LOGINFO("[Resolver] Loaded resolution for key: %s -> alias: %s, event: %s, eventHook: %s, permissionGroup: %s, includeContext: %s, useComRpc: %s",
                            key.c_str(), r.alias.c_str(), r.event.c_str(), r.eventHook.c_str(), r.permissionGroup.c_str(),
                            r.includeContext ? "true" : "false", r.useComRpc ? "true" : "false");

                    // Check if this resolution already exists (will be overridden)
                    if (mResolutions.find(key) != mResolutions.end())
                    {
                        LOGTRACE("[Resolver] Overriding resolution for key: %s", key.c_str());
                        overriddenCount++;
                    }

                    mResolutions[key] = std::move(r);
                    loadedCount++;
                }
            }

            LOGINFO("[Resolver] Loaded %zu resolutions from %s (%zu new, %zu overridden). Total resolutions: %zu",
                    loadedCount, path.c_str(), loadedCount - overriddenCount, overriddenCount, mResolutions.size());

            return true;
        }

        void Resolver::ClearResolutions()
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mResolutions.clear();
            LOGINFO("[Resolver] Cleared all resolutions");
        }

        bool Resolver::IsConfigured()
        {
            std::lock_guard<std::mutex> lock(mMutex);
            return !mResolutions.empty();
        }

        std::string Resolver::ResolveAlias(const std::string &key)
        {
            std::string lowerKey = StringUtils::toLower(key);
            std::lock_guard<std::mutex> lock(mMutex);
            auto it = mResolutions.find(lowerKey);
            if (it != mResolutions.end())
            {
                return it->second.alias;
            }
            return {}; // return empty if not found
        }

        void Resolver::ParseAlias(const std::string &alias, std::string &callsign, std::string &pluginMethod)
        {
            // Find last '.' in the string
            size_t dotPos = alias.rfind('.');

            if (dotPos != std::string::npos)
            {
                callsign = alias.substr(0, dotPos);      // "org.rdk.UserSettings"
                pluginMethod = alias.substr(dotPos + 1); // "getAudioDescription"
            }
            else
            {
                // Fallback: no dot found
                callsign = alias;
                pluginMethod = "";
            }

            LOGTRACE("[Resolver] Parsed alias '%s' -> callsign: '%s', method: '%s'",
                    alias.c_str(), callsign.c_str(), pluginMethod.c_str());
        }

        std::string Resolver::ExtractStringField(const WPEFramework::Core::JSON::VariantContainer &obj, const char *fieldName)
        {
            WPEFramework::Core::JSON::Variant field = obj[fieldName];
            if (field.IsSet() && !field.IsNull() && field.Content() == WPEFramework::Core::JSON::Variant::type::STRING)
            {
                return field.String();
            }
            return "";
        }

        bool Resolver::ExtractBooleanField(const WPEFramework::Core::JSON::VariantContainer &obj, const char *fieldName, bool defaultValue)
        {
            WPEFramework::Core::JSON::Variant field = obj[fieldName];
            if (field.IsSet() && !field.IsNull() && field.Content() == WPEFramework::Core::JSON::Variant::type::BOOLEAN)
            {
                return field.Boolean();
            }
            return defaultValue;
        }

        JsonValue Resolver::ExtractAdditionalContext(JsonObject &obj, const char *fieldName) 
        {
            return obj.Get(fieldName);
        }

        Core::hresult Resolver::CallThunderPlugin(const std::string &alias, const std::string &params, std::string &response)
        {
            if (mService == nullptr)
            {
                LOGERR("Shell service not set. Call setShell() first.");
                return Core::ERROR_GENERAL;
            }

            if (alias.empty())
            {
                LOGERR("Empty alias provided");
                return Core::ERROR_GENERAL;
            }

            std::string callsign;
            std::string pluginMethod;

            // Parse the alias to extract callsign and method
            ParseAlias(alias, callsign, pluginMethod);

            if (callsign.empty())
            {
                LOGERR("Failed to parse callsign from alias: %s", alias.c_str());
                return Core::ERROR_GENERAL;
            }

            if (pluginMethod.empty())
            {
                LOGERR("No method found in alias: %s", alias.c_str());
                return Core::ERROR_GENERAL;
            }

            auto thunderLink = Utils::GetThunderControllerClient(mService, callsign);
            if (!thunderLink)
            {
                LOGERR("Failed to create JSONRPCDirectLink for callsign: %s", callsign.c_str());
                return Core::ERROR_GENERAL;
            }

            Core::hresult result = thunderLink->Invoke<std::string, std::string>(pluginMethod, params, response);
            if (result != Core::ERROR_NONE)
            {
                LOGERR("Invoke failed for %s.%s, error code: %u",
                       callsign.c_str(), pluginMethod.c_str(), result);
            }
            return result;
        }

        bool Resolver::HasEvent(const std::string &key)
            {
                std::lock_guard<std::mutex> lock(mMutex);
                std::string lowerKey = StringUtils::toLower(key);
                auto it = mResolutions.find(lowerKey);
                if (it != mResolutions.end())
                {
                    return !it->second.event.empty();
                }
                return false;
            }

        bool Resolver::HasEventHook(const std::string &key, std::string &hookMethod)
            {
                std::lock_guard<std::mutex> lock(mMutex);
                std::string lowerKey = StringUtils::toLower(key);
                auto it = mResolutions.find(lowerKey);
                if (it != mResolutions.end() && !it->second.eventHook.empty())
                {
                    hookMethod = it->second.eventHook;
                    return true;
                }
                return false;
            }

        bool Resolver::HasIncludeContext(const std::string &key, JsonValue& additionalContext)
            {
                std::lock_guard<std::mutex> lock(mMutex);
                std::string lowerKey = StringUtils::toLower(key);
                auto it = mResolutions.find(lowerKey);
                if (it != mResolutions.end())
                {
                    if (it->second.additionalContext.IsSet()) {
                        additionalContext = it->second.additionalContext;
                    }
                    return it->second.includeContext;
                }
                return false;
            }

        bool Resolver::HasComRpcRequestSupport(const std::string &key) {
            std::string lowerKey = StringUtils::toLower(key);
            std::lock_guard<std::mutex> lock(mMutex);
            auto it = mResolutions.find(lowerKey);
            if (it != mResolutions.end())
            {
                return it->second.useComRpc;
            }
            return false;
        }

        bool Resolver::IsVersionedEvent(const std::string &key) {
            std::string lowerKey = StringUtils::toLower(key);
            std::lock_guard<std::mutex> lock(mMutex);
            auto it = mResolutions.find(lowerKey);
            if (it != mResolutions.end())
            {
                return it->second.versionedEvent;
            }
            return false;
        }

        bool Resolver::HasPermissionGroup(const std::string& key, std::string& permissionGroup )
        {
            std::lock_guard<std::mutex> lock(mMutex);
            std::string lowerKey = StringUtils::toLower(key);
            auto it = mResolutions.find(lowerKey);
            if (it != mResolutions.end())
            {
                permissionGroup = it->second.permissionGroup;
                return !permissionGroup.empty();
            }
            return false;
        }

    }
}

