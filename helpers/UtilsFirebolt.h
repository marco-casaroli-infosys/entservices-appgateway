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

#ifndef __UTILSFIREBOLT_H__
#define __UTILSFIREBOLT_H__

#include <mutex>
#include <map>

using namespace WPEFramework;

#define FIREBOLT_ERROR_NOT_SUPPORTED (-50100)
#define FIREBOLT_ERROR_NOT_AVAILABLE (-50200)
#define FIREBOLT_ERROR_NOT_PERMITTED (-40300)


class JListenRequest: public Core::JSON::Container {
    public:
        Core::JSON::Boolean listen;
    
    JListenRequest() : Core::JSON::Container()
    {
        Add(_T("listen"), &listen);
    }

    bool Get() const {
        return listen.Value();
    }
};

class JListenResponse: public Core::JSON::Container {
    public:
        Core::JSON::String event;
        Core::JSON::Boolean listening;
    
    JListenResponse() : Core::JSON::Container()
    {
        Add(_T("event"), &event);
        Add(_T("listening"), &listening);
    }
    
};

struct ProviderInfo{
    uint32_t channelId{0};
    uint32_t requestId{0};

    static ProviderInfo create(uint32_t chId, uint32_t reqId) {
        ProviderInfo info;
        info.channelId = chId;
        info.requestId = reqId;
        return info;
    }
};

class ProviderRegistry{
    public:
    void Add(const std::string& key, uint32_t chId, uint32_t reqId) {
        std::lock_guard<std::mutex> lock(mProviderMutex);
        mProviderMap[key] = ProviderInfo::create(chId, reqId);
    }

    void Add(const std::string& key, ProviderInfo provider) {
        std::lock_guard<std::mutex> lock(mProviderMutex);
        mProviderMap[key] = provider;
    }

    void Remove(const std::string& key) {
        std::lock_guard<std::mutex> lock(mProviderMutex);
        mProviderMap.erase(key);
    }

    void CleanupByConnectionId(const uint32_t connectionId) {
        std::lock_guard<std::mutex> lock(mProviderMutex);
        for (auto it = mProviderMap.begin(); it != mProviderMap.end(); ) {
            if (it->second.channelId == connectionId) {
                it = mProviderMap.erase(it);
            } else {
                ++it;
            }
        }
    }

    ProviderInfo Get(const std::string& key) {
        ProviderInfo info;
        std::lock_guard<std::mutex> lock(mProviderMutex);
        auto it = mProviderMap.find(key);
        if (it != mProviderMap.end()) {
            info = it->second;
        }
        return info;
    }

      private:            
        std::unordered_map<std::string, ProviderInfo> mProviderMap;
        std::mutex mProviderMutex;
};

enum FireboltError {
    NOT_SUPPORTED,
    NOT_AVAILABLE,
    NOT_PERMITTED
};

class ResponseUtils {
    public:
    static Core::hresult SetNullResponseForSuccess(const Core::hresult hResult, std::string& result) {
        if (Core::ERROR_NONE == hResult && result.empty()) {
            result = "null";
        }
        return hResult;
    }
};

class ErrorUtils {
    public:
    static std::string GetErrorMessageForFrameworkErrors(const uint32_t& errorCode, const std::string& message) {
        std::string errorMessage;
        Core::JSONRPC::Message::Info info;
        info.SetError(errorCode);
        info.Text = message;
        info.ToString(errorMessage);
        return errorMessage;
    }

    static std::string GetFireboltError(const FireboltError& error) {
        std::string errorMessage;
        Core::JSONRPC::Message::Info info;
        switch (error) {
            case FireboltError::NOT_SUPPORTED:
                info.Code = FIREBOLT_ERROR_NOT_SUPPORTED;
                info.Text = "NotSupported";
                break;
            case FireboltError::NOT_AVAILABLE:
                info.Code = FIREBOLT_ERROR_NOT_AVAILABLE;
                info.Text = "NotAvailable";
                break;
            case FireboltError::NOT_PERMITTED:
                info.Code = FIREBOLT_ERROR_NOT_PERMITTED;
                info.Text = "NotPermitted";
                break;
            default:
                errorMessage = "UnknownError";
                break;
        }
        info.ToString(errorMessage);
        return errorMessage;
    }

    static void NotSupported(string& resolution) {
        resolution = ErrorUtils::GetFireboltError(FireboltError::NOT_SUPPORTED);
    }

    static void NotAvailable(string& resolution) {
        resolution = ErrorUtils::GetFireboltError(FireboltError::NOT_AVAILABLE);
    }

    static void NotPermitted(string& resolution) {
        resolution = ErrorUtils::GetFireboltError(FireboltError::NOT_PERMITTED);
    }

    static void CustomInitialize(const string& message, string& resolution) {
        resolution = ErrorUtils::GetErrorMessageForFrameworkErrors(Core::ERROR_GENERAL, message);
    }

    static void CustomInternal(const string& message, string& resolution) {
        resolution = ErrorUtils::GetErrorMessageForFrameworkErrors(Core::ERROR_BAD_REQUEST, message);
    }

    static void CustomBadRequest(const string& message, string& resolution) {
        resolution = ErrorUtils::GetErrorMessageForFrameworkErrors(Core::ERROR_INVALID_SIGNATURE, message);
    }

    static void CustomBadMethod(const string& message, string& resolution) {
        resolution = ErrorUtils::GetErrorMessageForFrameworkErrors(Core::ERROR_INVALID_DESIGNATOR, message);
    }
    
};

#endif
