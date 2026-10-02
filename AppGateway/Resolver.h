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

#pragma once

#include "Module.h"
#include "UtilsLogging.h"
#include "StringUtils.h"
#include <unordered_map>
#include <mutex>
#include <core/Enumerate.h>

namespace WPEFramework
{
    namespace Plugin
    {

        // Configuration container for the entire resolutions file
        class ConfigContainer : public WPEFramework::Core::JSON::Container
        {
        public:
            ConfigContainer(const ConfigContainer &) = delete;
            ConfigContainer &operator=(const ConfigContainer &) = delete;

            ConfigContainer()
                : WPEFramework::Core::JSON::Container(), Resolutions()
            {
                Add(_T("resolutions"), &Resolutions);
            }

            WPEFramework::Core::JSON::VariantContainer Resolutions;
        };

        // Struct holding resolution info
        struct Resolution
        {
            std::string alias;
            std::string event;
            std::string eventHook;
            std::string permissionGroup;
            JsonValue additionalContext;
            bool includeContext = false;
            bool useComRpc = false;
            bool versionedEvent = false;
        };



        using namespace WPEFramework;
        class Resolver
        {
        public:
            Resolver(PluginHost::IShell *shell);
            ~Resolver();

            // Load resolutions from a JSON config file
            bool LoadConfig(const std::string &path);

            // Clear all existing resolutions
            void ClearResolutions();

            // Check if resolver has been properly configured
            bool IsConfigured();

            std::string ResolveAlias(const std::string &request);
            Core::hresult CallThunderPlugin(const std::string &alias, const std::string &params, std::string &response);

            // New Method to check if given method has ComRPC request ability
            bool HasComRpcRequestSupport(const std::string &key);

            // New method to check if the event field exists for a given key
            bool HasEvent(const std::string &key);

            // New method to check if an eventHook is configured for a given key
            bool HasEventHook(const std::string &key, std::string &hookMethod);

            // New method to check if includeContext is enabled for a given key
            bool HasIncludeContext(const std::string &key, JsonValue& additionalContext);

            // New method to check permission group is enabled
            bool HasPermissionGroup(const std::string& key, std::string& permissionGroup );

            // New method to check if the event is version based
            bool IsVersionedEvent(const std::string &key);

        private:
            void ParseAlias(const std::string &alias, std::string &callsign, std::string &pluginMethod);

            // Helper function to extract string field from JSON variant with type checking
            static std::string ExtractStringField(const WPEFramework::Core::JSON::VariantContainer &obj, const char *fieldName);

            // Helper function to extract boolean field from JSON variant with type checking
            static bool ExtractBooleanField(const WPEFramework::Core::JSON::VariantContainer &obj, const char *fieldName, bool defaultValue = false);
            JsonValue ExtractAdditionalContext(JsonObject &obj, const char *fieldName);
            
            
            PluginHost::IShell *mService;
            std::unordered_map<std::string, Resolution> mResolutions;
            std::mutex mMutex;
        };

        using ResolverPtr = std::shared_ptr<Resolver>;

    } // namespace Plugin
} // namespace WPEFramework

