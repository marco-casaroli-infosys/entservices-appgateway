/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
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

#pragma once

#include <cstddef>
#include <plugins/IShell.h>

#include "UtilsLogging.h"

namespace WPEFramework {
namespace Plugin {

template<typename T>
class ScopedInterface
{
public:
    ScopedInterface() = delete;

    ScopedInterface(PluginHost::IShell* shell, const char* callsign)
        : mPtr(nullptr)
    {
        const char* safeCallsign = (nullptr != callsign) ? callsign : "<null>";

        if (nullptr == shell)
        {
            LOGERR("shell is nullptr (callsign=%s)", safeCallsign);
            return;
        }

        if (nullptr == callsign)
        {
            LOGERR("callsign is nullptr");
            return;
        }

        mPtr = shell->QueryInterfaceByCallsign<T>(callsign);
        if (nullptr == mPtr)
        {
            LOGERR("QueryInterfaceByCallsign failed (callsign=%s)", safeCallsign);
            return;
        }

        LOGTRACE("interface acquired (callsign=%s)", safeCallsign);
    }

    ~ScopedInterface()
    {
        if (nullptr != mPtr)
        {
            mPtr->Release();
        }
    }

    T* get() const
    {
        return mPtr;
    }

    T* operator->() const
    {
        return mPtr;
    }

    explicit operator bool() const
    {
        return nullptr != mPtr;
    }

    bool operator==(std::nullptr_t) const
    {
        return nullptr == mPtr;
    }

    bool operator!=(std::nullptr_t) const
    {
        return nullptr != mPtr;
    }

    ScopedInterface(const ScopedInterface&) = delete;
    ScopedInterface& operator=(const ScopedInterface&) = delete;

private:
    T* mPtr;
};

} // namespace Plugin
} // namespace WPEFramework