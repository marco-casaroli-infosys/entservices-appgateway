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
#include "UtilsfileExists.h"

#ifndef __UTILSCALLSIGN_H__
#define __UTILSCALLSIGN_H__
#define APP_GATEWAY_CALLSIGN "org.rdk.AppGateway"
#define COMMON_GATEWAY_AUTHENTICATOR_CALLSIGN "org.rdk.AppGatewayCommon"
#define GATEWAY_AUTHENTICATOR_CALLSIGN "org.rdk.LaunchDelegate"
#define INTERNAL_GATEWAY_CALLSIGN GATEWAY_AUTHENTICATOR_CALLSIGN
#define APP_NOTIFICATIONS_CALLSIGN "org.rdk.AppNotifications"
#define APP_TO_APP_PROVIDER_CALLSIGN "org.rdk.App2AppProvider"
#define FB_PRIVACY_CALLSIGN "org.rdk.FbPrivacy"
#define FB_METRICS_CALLSIGN "org.rdk.FbMetrics"
#define ANALYTICS_PLUGIN_CALLSIGN "org.rdk.Analytics"
#ifndef RDKAPPMANAGERS_PATH
#define RDKAPPMANAGERS_PATH "/tmp/ai2managers"
#endif

// Use this class to check whether certain plugins are configured
class ConfigUtils {
    public:
        // Use this class to check whether App Managers are required to be used
        // Note: App Managers are not enabled by default in all build configurations.
        static bool useAppManagers() {
            if (Utils::fileExists(RDKAPPMANAGERS_PATH)) {
                return true;
            }
            return false;
        }
};
#endif