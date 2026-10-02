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
#ifndef __BASEEVENTDELEGATE_H__
#define __BASEEVENTDELEGATE_H__
#include "StringUtils.h"
#include <interfaces/IAppNotifications.h>
#include "UtilsLogging.h"
#include "UtilsCallsign.h"
#include <mutex>
#include <map>
#include <unordered_set>
#include <utility>

using namespace WPEFramework;

class BaseEventDelegate
{
public:
    class EXTERNAL EventDelegateDispatchJob : public Core::IDispatch
    {
    public:
        EventDelegateDispatchJob(BaseEventDelegate *delegate, const string &event, const string &payload, string appId = "")
            : mDelegate(*delegate), mEvent(event), mPayload(payload), mAppId(std::move(appId)) {}

        EventDelegateDispatchJob() = delete;
        EventDelegateDispatchJob(const EventDelegateDispatchJob &) = delete;
        EventDelegateDispatchJob &operator=(const EventDelegateDispatchJob &) = delete;
        ~EventDelegateDispatchJob()
        {
        }

        static Core::ProxyType<Core::IDispatch> Create(BaseEventDelegate *parent,
                                                       const string &event, const string &payload, string appId = "")
        {
            return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<EventDelegateDispatchJob>::Create(parent, event, payload, std::move(appId))));
        }

        virtual void Dispatch()
        {
            mDelegate.DispatchToAppNotifications(mEvent, mPayload, mAppId);
        }

    private:
        BaseEventDelegate &mDelegate;
        string mEvent;
        string mPayload;
        string mAppId;
    };

    BaseEventDelegate() : mRegisteredNotifications(),
                          mRegisterMutex()
    {
    }

    ~BaseEventDelegate()
    {
        // Cleanup registered notifications
        for (auto &entry : mRegisteredNotifications)
        {
            for (auto emitter : entry.second)
            {
                emitter->Release();
            }
        }

        mRegisteredNotifications.clear();
    }

    virtual bool HandleEvent(Exchange::IAppNotificationHandler::IEmitter *cb, const string &event, const bool listen, bool &registrationError) = 0;

    bool Dispatch(const string &event, const string &payload, string appId = "")
    {

        // Check if Notification is registered if not return
        if (!IsNotificationRegistered(event))
        {
            LOGDBG("Notification %s is not registered", event.c_str());
            return false;
        }

        Core::IWorkerPool::Instance().Submit(EventDelegateDispatchJob::Create(this, event, payload, std::move(appId)));

        return true;
    }

    bool DispatchToAppNotifications(const string &event, const string &payload, const string &appId)
    {

        auto emitters = GetEmittersForNotification(event);
        if (!emitters.empty())
        {
            LOGDBG("Using registered emitter for event %s", event.c_str());
            for (auto emitter : emitters)
            {
                emitter->Emit(event, payload, appId);
                emitter->Release();
            }
            return true;
        }

        LOGERR("No emitter found for event %s", event.c_str());
        return false;
    }

    // new method to register notifications
    // which accepts a string and adds it to the mRegisteredNotifications vector
    void AddNotification(const string &event, Exchange::IAppNotificationHandler::IEmitter *cb)
    {
        ASSERT(cb != nullptr);
        string event_l = StringUtils::toLower(event);
        std::lock_guard<std::mutex> lock(mRegisterMutex);
        auto it = mRegisteredNotifications.find(event_l);
        // add to the existing one
        if (it != mRegisteredNotifications.end())
        {
            // check if cb is already registered
            if (it->second.find(cb) != it->second.end())
            {
                LOGDBG("Notification %s already registered for this emitter", event_l.c_str());
            }
            else
            {
                it->second.insert(cb);
                cb->AddRef();
                LOGDBG("Added additional emitter for notification = %s", event_l.c_str());
            }
        }
        else
        {
            std::unordered_set<Exchange::IAppNotificationHandler::IEmitter *> emitters;
            emitters.insert(cb);
            mRegisteredNotifications[event_l] = std::move(emitters);
            cb->AddRef();
            LOGDBG("Notification registered = %s", event_l.c_str());
        }
    }

    // new method to check if a notification is registered
    bool IsNotificationRegistered(const string &event)
    {
        string event_l = StringUtils::toLower(event);
        std::lock_guard<std::mutex> lock(mRegisterMutex);
        bool result = mRegisteredNotifications.find(event_l) != mRegisteredNotifications.end();
        LOGTRACE("Finding notification = %s result=%s", event_l.c_str(), result ? "true" : "false");
        return result;
    }

    std::unordered_set<Exchange::IAppNotificationHandler::IEmitter *> GetEmittersForNotification(const string &event)
    {
        string event_l = StringUtils::toLower(event);
        std::unordered_set<Exchange::IAppNotificationHandler::IEmitter *> emitters;
        std::lock_guard<std::mutex> lock(mRegisterMutex);
        auto it = mRegisteredNotifications.find(event_l);
        if (it != mRegisteredNotifications.end())
        {
            emitters = it->second;
            for (auto emitter : emitters)
            {
                emitter->AddRef();
            }
        }
        return emitters;
    }

    // new method remove the notification
    void RemoveNotification(const string &event, Exchange::IAppNotificationHandler::IEmitter *cb)
    {
        ASSERT(cb != nullptr);
        string event_l = StringUtils::toLower(event);
        std::lock_guard<std::mutex> lock(mRegisterMutex);
        if (!event_l.empty())
        {
            // Remove specific emitter for the event
            auto it = mRegisteredNotifications.find(event_l);
            if (it != mRegisteredNotifications.end())
            {
                auto emitter = it->second.find(cb);
                if (emitter != it->second.end())
                {
                    (*emitter)->Release();
                    it->second.erase(emitter);
                    LOGDBG("Removed emitter for notification = %s", event_l.c_str());
                }

                // If no more emitters for the event, remove the event entry
                if (it->second.empty())
                {
                    mRegisteredNotifications.erase(it);
                    LOGDBG("No more emitters for notification = %s, event entry removed", event_l.c_str());
                }
            }
        }
        else
        { // For empty event, remove all events for the cb emitter

            for (auto it = mRegisteredNotifications.begin(); it != mRegisteredNotifications.end();)
            {
                auto emitter = it->second.find(cb);
                if (emitter != it->second.end())
                {
                    (*emitter)->Release();
                    it->second.erase(emitter);
                    LOGDBG("Removed emitter for notification = %s", it->first.c_str());
                }

                // If no more emitters for the event, remove the event entry
                if (it->second.empty())
                {
                    LOGDBG("No more emitters for notification = %s, event entry removed", it->first.c_str());
                    it = mRegisteredNotifications.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

private:
    std::map<string, std::unordered_set<Exchange::IAppNotificationHandler::IEmitter *>> mRegisteredNotifications;
    std::mutex mRegisterMutex;
};
#endif
