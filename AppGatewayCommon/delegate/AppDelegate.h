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
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#pragma once

#ifndef __APPDELEGATE_H__
#define __APPDELEGATE_H__
#include <mutex>
#include <interfaces/ISharedStorage.h>
#include <interfaces/IAppGateway.h>
#include "UtilsUUID.h"
#include "UtilsLogging.h"
#include "StringUtils.h"
#include "UtilsFirebolt.h"

#define ADVERTISING_ID_KEY "fireboltAdvertisingId"
#define ADVERTISING_TYPE "sessionid"
#define ADVERTISING_ID_LIMIT "1"
#define DEVICE_UID_KEY "fireboltDeviceUid"


using namespace WPEFramework;

class AppDelegate {
    public:
        AppDelegate(PluginHost::IShell *shell) : mShell(shell), mSharedStorage(nullptr) {}
        virtual ~AppDelegate() {
            {
                std::lock_guard<std::mutex> lock(mSharedStorageMutex);
                if (nullptr != mSharedStorage) {
                    mSharedStorage->Release();
                    mSharedStorage = nullptr;
                }
            }
        };

        Core::hresult GetDeviceUID(const std::string &appId, string &result /* @out */) {
            Exchange::ISharedStorage* sharedStorage = GetSharedStorage();
            if (nullptr != sharedStorage) {
                uint32_t ttl;
                bool success;
                if (Core::ERROR_NONE != sharedStorage->GetValue(Exchange::ISharedStorage::DEVICE, appId, DEVICE_UID_KEY, result, ttl, success)) {
                    // there is no existing value create a new one
                    result = UtilsUUID::GenerateUUID();
                    Exchange::ISharedStorage::Success successResult;
                    // TTL of 0 means its stored forever
                    ttl = 0;
                    sharedStorage->SetValue(Exchange::ISharedStorage::DEVICE, appId, DEVICE_UID_KEY, result, ttl, successResult);
                    if (successResult.success) {
                        result = "\"" + result + "\""; // Return the new UID as a JSON string value
                        return Core::ERROR_NONE;
                    } else {
                        LOGERR("Failed to set new Device UID in SharedStorage");
                        ErrorUtils::CustomInternal("Failed to set new Device UID in SharedStorage", result);
                        return Core::ERROR_GENERAL;
                    }
                }
                result = "\"" + result + "\""; // Return the UID as a string value
                return Core::ERROR_NONE;
            } else {
                LOGERR("Unable to get SharedStorage interface");
                ErrorUtils::CustomInternal("Unable to get SharedStorage interface", result);
                return Core::ERROR_UNAVAILABLE;
             }
         }

        Core::hresult GetAdvertisingId(const std::string &appId, string &result /* @out */) {
            Exchange::ISharedStorage* sharedStorage = GetSharedStorage();
            string ifa;
            if (nullptr != sharedStorage) {
                uint32_t ttl;
                bool success;
                if (Core::ERROR_NONE != sharedStorage->GetValue(Exchange::ISharedStorage::DEVICE, appId, ADVERTISING_ID_KEY, ifa, ttl, success)) {
                    // there is no existing value create a new one
                    ifa = UtilsUUID::GenerateUUID();
                    Exchange::ISharedStorage::Success successResult;
                    // TTL of 0 means its stored forever
                    ttl = 0;

                    sharedStorage->SetValue(Exchange::ISharedStorage::DEVICE, appId, ADVERTISING_ID_KEY, ifa, ttl, successResult);
                    if (!successResult.success) {
                        LOGERR("Failed to set new Advertising ID in SharedStorage");
                        ErrorUtils::CustomInternal("Failed to set new Advertising ID in SharedStorage", result);
                        return Core::ERROR_GENERAL;
                    }
                }

                JsonObject response;
                response["ifa"] = ifa;
                response["ifa_type"] = ADVERTISING_TYPE;
                response["lmt"] = ADVERTISING_ID_LIMIT;
                response.ToString(result);
                return Core::ERROR_NONE;
            } else {
                LOGERR("Unable to get SharedStorage interface");
                 ErrorUtils::CustomInternal("Unable to get SharedStorage interface", result);
                return Core::ERROR_UNAVAILABLE;
            }
        }

       Core::hresult HandleAppGatewayRequest(const Exchange::GatewayContext& context ,
                                          const string& method ,
                                          const string& payload /*@opaque */,
                                          string& result /*@out @opaque */) {
            string lowerMethod = StringUtils::toLower(method);
            if ("advertising.advertisingid" == lowerMethod) {
                return GetAdvertisingId(context.appId, result);
            } else if ("device.uid" == lowerMethod) {
                return GetDeviceUID(context.appId, result);
            }
            
            ErrorUtils::CustomInternal("Not Supported", result);
            return Core::ERROR_UNAVAILABLE;
        }

        Exchange::ISharedStorage* GetSharedStorage() {
            std::lock_guard<std::mutex> lock(mSharedStorageMutex);
            if (nullptr == mSharedStorage && nullptr != mShell) {
                mSharedStorage = mShell->QueryInterfaceByCallsign<Exchange::ISharedStorage>("org.rdk.SharedStorage");
                if (nullptr == mSharedStorage) {
                    LOGERR("Failed to get SharedStorage COM interface");
                }
            }
            return mSharedStorage;
        }
    private:
        mutable std::mutex mSharedStorageMutex;
        PluginHost::IShell *mShell;
        Exchange::ISharedStorage *mSharedStorage;
};

#endif