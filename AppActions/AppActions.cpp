#include "AppActions.h"
#include "AppActionsImplementation.h"
#include <plugins/IShell.h>

#define API_VERSION_NUMBER_MAJOR    APPACTIONS_MAJOR_VERSION
#define API_VERSION_NUMBER_MINOR    APPACTIONS_MINOR_VERSION
#define API_VERSION_NUMBER_PATCH    APPACTIONS_PATCH_VERSION

namespace WPEFramework {
namespace Plugin {
    SERVICE_REGISTRATION(AppActions, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

    AppActions::AppActions() : PluginHost::IPlugin(), PluginHost::JSONRPC(), mService(nullptr), mConnectionId(0), mAppActions(nullptr), mAppActionsConfigure(nullptr), mAppActionsNotification(this) {
    }

    AppActions::~AppActions() {
    }

    const string AppActions::Initialize(PluginHost::IShell* service) {
        // Initialization logic
        string message = "";
        ASSERT(nullptr != service);
        ASSERT(nullptr == mService);
        ASSERT(nullptr == mAppActions);
        ASSERT(0 == mConnectionId);

        SYSLOG(Logging::Startup, (_T("[%s] Initialize entry PID=%u"), __FUNCTION__, getpid()));
        mService = service;
        mService->AddRef();

        // Register for out-of-process connection state changes so Notification::Deactivated
        // is called if the AppActionsImplementation host process crashes.
        // COMLink is null in in-process / test environments — warn but continue, as the
        // plugin is still functional; crash-recovery simply won't be available.
        PluginHost::IShell::ICOMLink* comLink = mService->COMLink();
        if (nullptr != comLink) {
            comLink->Register(&mAppActionsNotification);
        } else {
            SYSLOG(Logging::Startup, (_T("AppActions::Initialize: COMLink is not available; crash recovery will be unavailable")));
        }

        mAppActions = mService->Root<Exchange::IAppActions>(mConnectionId, 5000, _T("AppActionsImplementation"));
        if (nullptr == mAppActions)
        {
            LOGERR("Failed to initialise AppActions plugin!");
            SYSLOG(Logging::Startup, (_T("AppActions::Initialize: object creation failed")));
            message = _T("AppActions plugin could not be initialised");
        } else {
            mAppActionsConfigure = mAppActions->QueryInterface<Exchange::IConfiguration>();
            if (nullptr == mAppActionsConfigure)
            {
                LOGERR("Failed to get IConfiguration interface from AppActions plugin!");
                SYSLOG(Logging::Startup, (_T("AppActions::Initialize: IConfiguration interface not found")));
                message = _T("AppActions plugin could not be initialised due to missing IConfiguration interface");
            } else {
                if (Core::ERROR_NONE == mAppActionsConfigure->Configure(mService))
                {
                    mAppActions->Register(&mAppActionsNotification);
                    //Invoking Plugin API register to wpeframework
                    Exchange::JAppActions::Register(*this, mAppActions);
                } else {
                    SYSLOG(Logging::Startup, (_T("AppActions::Initialize: could not be configured")));
                    message = _T("AppActions could not be configured");
                }
            }
        }

        SYSLOG(Logging::Startup, (_T("[%s] Initialize exit status=%s"), __FUNCTION__, message.empty() ? "success" : "failed"));
        // On success return empty, to indicate there is no error text.
        return message;
    }

    void AppActions::Deinitialize(PluginHost::IShell* service) {
        // Deinitialization logic
        // Guard: if mService is null the plugin was never initialised or was already
        // deinitialized — treat as a no-op to match the same pattern applied to
        // AppActionsImplementation::Deinitialize and avoid an abort on double-Deinitialize.
        SYSLOG(Logging::Shutdown, (_T("[%s] Deinitialize entry"), __FUNCTION__));
        if (nullptr == mService) {
            SYSLOG(Logging::Shutdown, (_T("[%s] Deinitialize exit: no-op (not initialized)"), __FUNCTION__));
            return;
        }
        ASSERT(mService == service);

        RPC::IRemoteConnection *connection = nullptr;
        VARIABLE_IS_NOT_USED uint32_t result = Core::ERROR_NONE;

        // Make sure the Activated and Deactivated are no longer called before we start cleaning up..
        // COMLink may be null in in-process / test environments — skip OOP-specific cleanup
        // in that case but always proceed to release mAppActions and mService below.
        PluginHost::IShell::ICOMLink* comLink = mService->COMLink();
        if (nullptr != comLink) {
            comLink->Unregister(&mAppActionsNotification);

            if (0 != mConnectionId) {
                // Use comLink rather than service->RemoteConnection(): the latter is not
                // a pure virtual in IShell (no MOCK_METHOD in ServiceMock) and its default
                // base implementation routes through COMLink anyway.
                connection = comLink->RemoteConnection(mConnectionId);
                // If this was running in a (container) process...
                if (nullptr != connection)
                {
                    // Lets trigger the cleanup sequence for
                    // out-of-process code. Which will guard
                    // that unwilling processes, get shot if
                    // not stopped friendly :-)
                    connection->Terminate();
                    connection->Release();
                }
            } else {
                SYSLOG(Logging::Shutdown, (_T("[%s] Deinitialize exit: no-op (no remote connection)"), __FUNCTION__));
            }
        } else {
            SYSLOG(Logging::Shutdown, (_T("[%s] Deinitialize exit: no-op (no COMLink)"), __FUNCTION__));
        }

        if (nullptr != mAppActionsConfigure)
        {
            mAppActionsConfigure->Release();
            mAppActionsConfigure = nullptr;
        }
        if (nullptr != mAppActions)
        {
            mAppActions->Unregister(&mAppActionsNotification);
            Exchange::JAppActions::Unregister(*this);
            result = mAppActions->Release();
            mAppActions = nullptr;
        }
        mConnectionId = 0;
        mService->Release();
        mService = nullptr;
        SYSLOG(Logging::Shutdown, (_T("[%s] Deinitialize exit"), __FUNCTION__));
    }

    string AppActions::Information() const {
        return string();
    }

    void AppActions::Deactivated(RPC::IRemoteConnection *connection)
    {
        if (connection->Id() == mConnectionId)
        {
            ASSERT(nullptr != mService);
            Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(mService, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
        }
    }

} // namespace Plugin
} // namespace WPEFramework
