// AppActionsImplementation.h must be first: it pulls in AppActions/Module.h which
// defines MODULE_NAME=Plugin_AppActions. interfaces/Module.h (included transitively
// by IAppGateway.h inside UtilsAppGatewayTelemetry.h) has the guard:
//   #ifndef MODULE_NAME
//   #define MODULE_NAME Interfaces   // ← the C++ symbol in libWPEFrameworkInterfaces
//   #endif
// If UtilsAppGatewayTelemetry.h is included first, MODULE_NAME becomes "Interfaces"
// and SERVICE_REGISTRATION references Core::System::Interfaces, which is not linked
// into this shared library → "undefined reference to 'Interfaces'" linker error.
#include "AppActionsImplementation.h"
#include "UtilsLogging.h"
#include "UtilsAppGatewayTelemetry.h"
#include <plugins/IShell.h>

#include <vector>

#define API_VERSION_NUMBER_MAJOR    APPACTIONS_MAJOR_VERSION
#define API_VERSION_NUMBER_MINOR    APPACTIONS_MINOR_VERSION
#define API_VERSION_NUMBER_PATCH    APPACTIONS_PATCH_VERSION

AGW_DEFINE_TELEMETRY_CLIENT(AGW_PLUGIN_APPACTIONS)

namespace WPEFramework {
namespace Plugin {

    SERVICE_REGISTRATION(AppActionsImplementation, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

    AppActionsImplementation::AppActionsImplementation(): mService(nullptr), mAppActionsNotifications() {
        // Constructor implementation
        //SYSLOG(Logging::Startup, (_T("AppActionsImplementation Constructor")));
        LOGINFO("AppActionsImplementation Constructor");
    }

    AppActionsImplementation::~AppActionsImplementation() {
        // Ensure the static TelemetryClient is cleaned up even when
        // AppActionsImplementation::Deinitialize() was never called.
        // AppActions::Deinitialize() only calls mAppActions->Release() which
        // destroys the impl without going through Deinitialize(), leaving the
        // static TelemetryClient with a dangling mService pointer.
        // Calling DEINIT here is safe: if Deinitialize() already ran it is a no-op.
        AGW_TELEMETRY_DEINIT();
        LOGINFO("AppActionsImplementation Destructor");
    }

    Core::hresult AppActionsImplementation::ActionStart(const string& initiator, const string& intent, const string& handlerAppId)
    {
        LOGDBG("ActionStart called with initiator: %s, intent: %s, handlerAppId: %s", initiator.c_str(), intent.c_str(), handlerAppId.c_str());
        // Dispatch notification asynchronously to avoid blocking the caller
        Core::IWorkerPool::Instance().Submit(NotifyJob::Create(this, initiator, intent, handlerAppId));
        return Core::ERROR_NONE;
    }
    /**
     * Register a notification callback
     */
    Core::hresult AppActionsImplementation::Register(Exchange::IAppActions::INotification *notification)
    {
        if (nullptr == notification) {
            LOGERR("Register called with null notification");
            return Core::ERROR_BAD_REQUEST;
        }
        Core::hresult status = Core::ERROR_GENERAL;
        bool alreadyRegistered = false;

        {
            std::lock_guard<std::mutex> lock(mAdminLock);
            if (std::find(mAppActionsNotifications.begin(), mAppActionsNotifications.end(), notification) == mAppActionsNotifications.end()) {
                LOGINFO("Register notification");
                mAppActionsNotifications.push_back(notification);
                notification->AddRef();
                status = Core::ERROR_NONE;
            } else {
                LOGWARN("notification already registered");
                alreadyRegistered = true;
            }
        } // mAdminLock released here before any COM-RPC calls

        // AGW_REPORT_API_ERROR calls IsAvailable() which may call QueryInterfaceByCallsign
        // (COM-RPC), and then RecordTelemetryEvent (another COM-RPC). Both must be made
        // outside mAdminLock to avoid deadlock.
        if (alreadyRegistered) {
            Exchange::GatewayContext ctx{};
            AGW_REPORT_API_ERROR(ctx, "Register", AGW_ERROR_ALREADY_REGISTERED);
        }
        return status;
    }
    /**
     * Unregister a notification callback
     */
    Core::hresult AppActionsImplementation::Unregister(Exchange::IAppActions::INotification *notification)
    {
        if (nullptr == notification) {
            LOGERR("Unregister called with null notification");
            return Core::ERROR_BAD_REQUEST;
        }
        Core::hresult status = Core::ERROR_GENERAL;
        std::lock_guard<std::mutex> lock(mAdminLock);

        auto itr = std::find(mAppActionsNotifications.begin(), mAppActionsNotifications.end(), notification);
        if (itr != mAppActionsNotifications.end())
        {
            (*itr)->Release();
            LOGINFO("Unregister notification");
            mAppActionsNotifications.erase(itr);
            status = Core::ERROR_NONE;
        } else {
            LOGWARN("notification not found");
        }
        return status;
    }

    Core::hresult AppActionsImplementation::Configure(PluginHost::IShell* service)
    {
        Core::hresult status = Core::ERROR_GENERAL;
        SYSLOG(Logging::Startup, (_T("AppActionsImplementation Configure entry")));
        if (nullptr != service) {
            {
                std::lock_guard<std::mutex> lock(mAdminLock);
                if (nullptr != mService) {
                    mService->Release();
                    mService = nullptr;
                }
                mService = service;
                mService->AddRef();
                status = Core::ERROR_NONE;
            }
            // Initialize telemetry OUTSIDE mAdminLock: QueryInterfaceByCallsign is
            // a COM-RPC call; holding the mutex across it risks deadlock.
            // TelemetryClient stores mService and lazy-reconnects via IsAvailable()
            // if AppGateway is not yet active at this point.
            AGW_TELEMETRY_INIT(service);
            // SYSLOG so init status appears in the WPEFramework syslog stream.
            // LOGINFO/LOGWARN inside TelemetryClient go to stderr (fprintf), not syslog.
            // If AppGateway is not yet active the client stores mService and lazy-reconnects
            // via IsAvailable() the first time a telemetry event is reported.
            SYSLOG(Logging::Startup, (_T("AppActionsImplementation telemetry client: %s"),
                AGW_TELEMETRY_CLIENT_ACCESSOR().IsAvailable()
                    ? "connected to AppGateway"
                    : "AppGateway not yet active - will lazy-connect on first use"));
            SYSLOG(Logging::Startup, (_T("AppActionsImplementation service configured successfully")));
        } else {
            SYSLOG(Logging::Startup, (_T("AppActionsImplementation service configuration failed: service is null")));
        }
        SYSLOG(Logging::Startup, (_T("AppActionsImplementation Configure exit status=%s"), status == Core::ERROR_NONE ? "success" : "failed"));
        return status;
    }

    const string AppActionsImplementation::Initialize(PluginHost::IShell* service)
    {
         SYSLOG(Logging::Notification, (_T("[%s] Initialize entry"), __FUNCTION__));
         string result = (Configure(service) == Core::ERROR_NONE) ? string() : _T("Failed to configure AppActionsImplementation plugin");
         SYSLOG(Logging::Notification, (_T("[%s] Initialize exit"), __FUNCTION__));
         return std::move(result);
    }

    void AppActionsImplementation::Deinitialize(PluginHost::IShell* service)
    {
        SYSLOG(Logging::Shutdown, (_T("AppActionsImplementation Deinitialize entry")));
        AGW_TELEMETRY_DEINIT();
        std::lock_guard<std::mutex> lock(mAdminLock);
        if (nullptr != mService) {
            // Only assert when mService is non-null; asserting against nullptr is
            // invalid and aborts when Deinitialize is called without prior Initialize.
            ASSERT(mService == service);
            SYSLOG(Logging::Shutdown, (_T("AppActions Deinitialize Unregistering notifications and releasing service")));
            for (auto* notification : mAppActionsNotifications) {
                notification->Release();
            }
            mAppActionsNotifications.clear();
            mService->Release();
            mService = nullptr;
        }
        SYSLOG(Logging::Shutdown, (_T("AppActionsImplementation Deinitialize exit")));
    }

    string AppActionsImplementation::Information() const {
        return string();
    }

    void AppActionsImplementation::DispatchActionStartRequest(
        const string& initiator, const string& intent, const string& handlerAppId)
    {
        LOGDBG("Dispatching ActionStartRequest to notifications: initiator=%s, intent=%s, handlerAppId=%s",
            initiator.c_str(), intent.c_str(), handlerAppId.c_str());

        std::vector<Exchange::IAppActions::INotification*> notifications;

        {
            std::lock_guard<std::mutex> lock(mAdminLock);
            notifications.assign(mAppActionsNotifications.begin(), mAppActionsNotifications.end());

            for (auto* notification : notifications) {
                if (nullptr != notification) {
                    notification->AddRef();
                }
            }
        }

        for (auto* notification : notifications) {
            if (nullptr != notification) {
                notification->OnActionStartRequest(initiator, intent, handlerAppId);
                notification->Release();
            }
        }
    }
} // namespace Plugin
} // namespace WPEFramework
