/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
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
 */

#include "AppNotifications.h"
#include "UtilsAppGatewayTelemetry.h"
#include <interfaces/IConfiguration.h>


#define API_VERSION_NUMBER_MAJOR    APPNOTIFICATIONS_MAJOR_VERSION
#define API_VERSION_NUMBER_MINOR    APPNOTIFICATIONS_MINOR_VERSION
#define API_VERSION_NUMBER_PATCH    APPNOTIFICATIONS_PATCH_VERSION

AGW_DEFINE_TELEMETRY_CLIENT(AGW_PLUGIN_APPNOTIFICATIONS)

namespace WPEFramework {

namespace {
    static Plugin::Metadata<Plugin::AppNotifications> metadata(
        // Version (Major, Minor, Patch)
        API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
        // Preconditions
        {},
        // Terminations
        {},
        // Controls
        {}
    );
}

namespace Plugin {
    SERVICE_REGISTRATION(AppNotifications, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

    AppNotifications::AppNotifications(): mService(nullptr), mAppNotifications(nullptr), mConnectionId(0)
    {
        SYSLOG(Logging::Startup, (_T("AppNotifications Constructor")));
    }

    AppNotifications::~AppNotifications()
    {
        SYSLOG(Logging::Shutdown, (string(_T("AppNotifications Destructor"))));
    }

    /* virtual */ const string AppNotifications::Initialize(PluginHost::IShell* service)
    {
        ASSERT(service != nullptr);
        ASSERT(mAppNotifications == nullptr);

        SYSLOG(Logging::Startup, (_T("AppNotifications::Initialize: PID=%u"), getpid()));

        mService = service;
        mService->AddRef();

        AGW_TELEMETRY_INIT(mService);
        AGW_RECORD_BOOTSTRAP_TIME();

        mAppNotifications = service->Root<Exchange::IAppNotifications>(mConnectionId, 2000, _T("AppNotificationsImplementation"));

        if (mAppNotifications != nullptr) {
            auto configConnection = mAppNotifications->QueryInterface<Exchange::IConfiguration>();
            if (configConnection != nullptr) {
                configConnection->Configure(service);
                configConnection->Release();
            }
        }
        else
        {
            SYSLOG(Logging::Startup, (_T("AppNotifications::Initialize: Failed to initialise AppNotifications plugin")));
        }
        // On success return empty, to indicate there is no error text.
        return ((mAppNotifications != nullptr))
            ? EMPTY_STRING
            : _T("Could not retrieve the AppNotifications interface.");
    }

    /* virtual */ void AppNotifications::Deinitialize(PluginHost::IShell* service)
    {
        SYSLOG(Logging::Shutdown, (string(_T("AppNotifications::Deinitialize"))));
        ASSERT(service == mService);

        AGW_TELEMETRY_DEINIT();

        if (mAppNotifications != nullptr) {
            RPC::IRemoteConnection *connection(service->RemoteConnection(mConnectionId));
            VARIABLE_IS_NOT_USED uint32_t result = mAppNotifications->Release();
            mAppNotifications = nullptr;

            // It should have been the last reference we are releasing,
            // so it should end up in a DESCRUCTION_SUCCEEDED, if not we
            // are leaking...

            // If this was running in a (container) process...
            if (connection != nullptr)
            {
                // Lets trigger a cleanup sequence for
                // out-of-process code. Which will guard
                // that unwilling processes, get shot if
                // not stopped friendly :~)
                connection->Terminate();
                connection->Release();
            }
        }

        mConnectionId = 0;
        mService->Release();
        mService = nullptr;
        SYSLOG(Logging::Shutdown, (string(_T("AppNotifications de-initialised"))));
    }

    void AppNotifications::Deactivated(RPC::IRemoteConnection* connection)
    {
        if (connection->Id() == mConnectionId) {

            ASSERT(mService != nullptr);

            Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(mService, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
        }
    }

} // namespace Plugin
} // namespace WPEFramework