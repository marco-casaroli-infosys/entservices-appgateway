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

#ifndef __SETTINGSDELEGATE_H__
#define __SETTINGSDELEGATE_H__
#include "StringUtils.h"
#include "UserSettingsDelegate.h"
#include "SystemDelegate.h"
#include "NetworkDelegate.h"
#include "LifecycleDelegate.h"
#include "UtilsLogging.h"
#include "AppDelegate.h"
#include "TTSDelegate.h"
#include "VideoOutputDelegate.h"
#include "AvOutputDelegate.h"
#include <interfaces/IAppNotifications.h>

#define APP_NOTIFICATIONS_CALLSIGN "org.rdk.AppNotifications"
using namespace WPEFramework;

class SettingsDelegate {
    public:
        SettingsDelegate(): userSettings(nullptr), systemDelegate(nullptr), networkDelegate(nullptr), lifecycleDelegate(nullptr), appDelegate(nullptr), ttsDelegate(nullptr), avOutputDelegate(nullptr), videoOutputDelegate(nullptr) {}

        ~SettingsDelegate() {
            userSettings = nullptr;
            systemDelegate = nullptr;
            networkDelegate = nullptr;
            lifecycleDelegate = nullptr;
            appDelegate = nullptr;
            ttsDelegate = nullptr;
            avOutputDelegate = nullptr;
            videoOutputDelegate = nullptr;
        }

        void HandleAppEventNotifier(Exchange::IAppNotificationHandler::IEmitter *cb, const string event,
                                    const bool listen) {
            bool registrationError = false;
            if (userSettings==nullptr || systemDelegate==nullptr || networkDelegate==nullptr || lifecycleDelegate==nullptr || ttsDelegate==nullptr || avOutputDelegate==nullptr || nullptr == videoOutputDelegate) {
                LOGERR("Services not available");
                return;
            }

            std::vector<std::shared_ptr<BaseEventDelegate>> delegates = {userSettings, systemDelegate, networkDelegate, lifecycleDelegate, ttsDelegate, avOutputDelegate, videoOutputDelegate};
            bool handled = false;

            for (const auto& delegate : delegates) {
                if (nullptr == delegate) {
                    continue;
                }
                if (delegate->HandleEvent(cb, event, listen, registrationError)) {
                    handled = true;
                    break;
                }
            }

            if (!handled) {
                LOGERR("No Matching registrations");
            }

            if (registrationError) {
                LOGERR("Error in registering/unregistering for event %s", event.c_str());
            }
        }

        void setShell(PluginHost::IShell* shell) {

            ASSERT(shell != nullptr);
            LOGDBG("SettingsDelegate::setShell");

            if (userSettings == nullptr) {
                userSettings = std::make_shared<UserSettingsDelegate>(shell);
            }

            if (systemDelegate == nullptr) {
                systemDelegate = std::make_shared<SystemDelegate>(shell);
            }

            if (networkDelegate == nullptr) {
                networkDelegate = std::make_shared<NetworkDelegate>(shell);
            }

            if (lifecycleDelegate == nullptr) {
                lifecycleDelegate = std::make_shared<LifecycleDelegate>(shell);
            }

            if (appDelegate == nullptr) {
                appDelegate = std::make_shared<AppDelegate>(shell);
            }

            if (ttsDelegate == nullptr) {
                ttsDelegate = std::make_shared<TTSDelegate>(shell);
            }

            if (avOutputDelegate == nullptr) {
                avOutputDelegate = std::make_shared<AvOutputDelegate>(shell);
            }

            if (videoOutputDelegate == nullptr) {
                videoOutputDelegate = std::make_shared<VideoOutputDelegate>(shell);
            }
        }

        void Cleanup() {
            systemDelegate.reset();
            userSettings.reset();
            networkDelegate.reset();
            lifecycleDelegate.reset();
            appDelegate.reset();
            ttsDelegate.reset();
            avOutputDelegate.reset();
            videoOutputDelegate.reset();
        }

        std::shared_ptr<SystemDelegate> getSystemDelegate() const {
            return systemDelegate;
        }

        std::shared_ptr<UserSettingsDelegate> getUserSettings() const {
            return userSettings;
        }

        std::shared_ptr<NetworkDelegate> getNetworkDelegate() const {
            return networkDelegate;
        }

        std::shared_ptr<LifecycleDelegate> getLifecycleDelegate() const {
            return lifecycleDelegate;
        }

        std::shared_ptr<AppDelegate> getAppDelegate() const {
            return appDelegate;
        }

        std::shared_ptr<AvOutputDelegate> getAvOutputDelegate() const {
            return avOutputDelegate;
        }

        std::shared_ptr<VideoOutputDelegate> getVideoOutputDelegate() const {
            return videoOutputDelegate;
        }

        std::shared_ptr<TTSDelegate> getTTSDelegate() const {
            return ttsDelegate;
        }

    private:
        std::shared_ptr<UserSettingsDelegate> userSettings;
        std::shared_ptr<SystemDelegate> systemDelegate;
        std::shared_ptr<NetworkDelegate> networkDelegate;
        std::shared_ptr<LifecycleDelegate> lifecycleDelegate;
        std::shared_ptr<AppDelegate> appDelegate;
        std::shared_ptr<TTSDelegate> ttsDelegate;
        std::shared_ptr<AvOutputDelegate> avOutputDelegate;
        std::shared_ptr<VideoOutputDelegate> videoOutputDelegate;
};

#endif
