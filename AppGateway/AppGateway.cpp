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

#include "AppGateway.h"
#include "AppGatewayTelemetry.h"
#include "UtilsAppGatewayTelemetry.h"
#include <interfaces/IConfiguration.h>
#include <interfaces/json/JsonData_AppGatewayResolver.h>
#include <interfaces/json/JAppGatewayResolver.h>
#include "UtilsLogging.h"
#include <chrono>


#define API_VERSION_NUMBER_MAJOR    APPGATEWAY_MAJOR_VERSION
#define API_VERSION_NUMBER_MINOR    APPGATEWAY_MINOR_VERSION
#define API_VERSION_NUMBER_PATCH    APPGATEWAY_PATCH_VERSION

// Timeout (ms) for COM-RPC out-of-process object instantiation via IShell::Root()
static constexpr uint32_t COMRPC_CONNECT_TIMEOUT_MS = 2000;


namespace WPEFramework {

namespace {
    static Plugin::Metadata<Plugin::AppGateway> metadata(
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
    SERVICE_REGISTRATION(AppGateway, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

    AppGateway::AppGateway()
            : PluginHost::JSONRPC(), mService(nullptr), mAppGateway(nullptr), mResponder(nullptr), mTelemetry(nullptr), mConnectionId(0)
        {

        LOGINFO("AppGateway Constructor");
    }

    AppGateway::~AppGateway()
    {
    }

    /* virtual */ const string AppGateway::Initialize(PluginHost::IShell* service)
    {
        ASSERT(service != nullptr);
        ASSERT(mAppGateway == nullptr);
        ASSERT(mResponder == nullptr);
        ASSERT(mTelemetry == nullptr);

        LOGINFO("AppGateway::Initialize: PID=%u", getpid());
        // Measure bootstrap time
        auto bootstrapStart = std::chrono::steady_clock::now();

        mService = service;
        mService->AddRef();

        // Initialize telemetry aggregator first (singleton)
        AppGatewayTelemetry::getInstance().Initialize(service);
        // Set the telemetry interface pointer for COM-RPC exposure
        mTelemetry = &AppGatewayTelemetry::getInstance();
        mTelemetry->AddRef();

        // Initialize the module-local TelemetryClient so that AGW_TIME_JOB
        // in AppGatewayImplementation / AppGatewayResponderImplementation jobs
        // can reach the telemetry singleton via COM-RPC.
        AGW_TELEMETRY_INIT(service);

        mAppGateway = service->Root<Exchange::IAppGatewayResolver>(mConnectionId, COMRPC_CONNECT_TIMEOUT_MS, _T("AppGatewayImplementation"));
       
        if (mAppGateway != nullptr) {
            auto configConnection = mAppGateway->QueryInterface<Exchange::IConfiguration>();
            if (configConnection != nullptr) {
                configConnection->Configure(service);
                configConnection->Release();
            }

            //Invoking Plugin API register to wpeframework
            Exchange::JAppGatewayResolver::Register(*this, mAppGateway);
        }
        else
        {
            LOGERR("Failed to initialise AppGatewayResolver plugin!");
        }

        mResponder = service->Root<Exchange::IAppGatewayResponder>(mConnectionId, COMRPC_CONNECT_TIMEOUT_MS, _T("AppGatewayResponderImplementation"));
        if (mResponder != nullptr) {
            auto configConnectionResponder = mResponder->QueryInterface<Exchange::IConfiguration>();
            if (configConnectionResponder != nullptr) {
                configConnectionResponder->Configure(service);
                configConnectionResponder->Release();
            }
        }
        else
        {
            LOGERR("Failed to initialise AppGatewayResponder plugin!");
        }
   
        // Record bootstrap time (AppGateway uses direct singleton access)
        auto bootstrapEnd = std::chrono::steady_clock::now();
        // Use microseconds for precision, then convert to milliseconds as double
        auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(
            bootstrapEnd - bootstrapStart).count();
        double durationMs = durationUs / 1000.0;  // Convert to milliseconds with decimal precision
        AppGatewayTelemetry::getInstance().RecordBootstrapTime(durationMs);
            
        // On success return empty, to indicate there is no error text.
        return ((mAppGateway != nullptr) && (mResponder != nullptr))
            ? EMPTY_STRING
            : _T("Could not retrieve the AppGateway interface.");
    }

    /* virtual */ void AppGateway::Deinitialize(PluginHost::IShell* service)
    {
        ASSERT(service == mService);

        RPC::IRemoteConnection *connection = nullptr;
        VARIABLE_IS_NOT_USED uint32_t result = Core::ERROR_NONE;

        if ((mAppGateway != nullptr) || (mResponder != nullptr)) {
            connection = service->RemoteConnection(mConnectionId);
        }

        // Deinitialize the module-local TelemetryClient (matches AGW_TELEMETRY_INIT in Initialize)
        AGW_TELEMETRY_DEINIT();

        // Deinitialize telemetry aggregator singleton
        AppGatewayTelemetry::getInstance().Deinitialize();
        if (mTelemetry != nullptr) {
            mTelemetry->Release();
            mTelemetry = nullptr;
        }
        LOGINFO("AppGatewayTelemetry deinitialized");

        if (mResponder != nullptr) {
            result = mResponder->Release();
            mResponder = nullptr;

            // It should have been the last reference we are releasing,
            // so it should end up in a DESTRUCTION_SUCCEEDED, if not we
            // are leaking...
            ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);
        }

        if (mAppGateway != nullptr) {
            Exchange::JAppGatewayResolver::Unregister(*this);
            result = mAppGateway->Release();
            mAppGateway = nullptr;

            // It should have been the last reference we are releasing,
            // so it should end up in a DESTRUCTION_SUCCEEDED, if not we
            // are leaking...
            ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);
        }

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

        mConnectionId = 0;
        mService->Release();
        mService = nullptr;
    }

    void AppGateway::Deactivated(RPC::IRemoteConnection* connection)
    {
        if (connection->Id() == mConnectionId) {

            ASSERT(mService != nullptr);

            Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(mService, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
        }
    }

} // namespace Plugin
} // namespace WPEFramework
