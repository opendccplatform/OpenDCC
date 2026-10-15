// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/utils/debug.h"
#ifdef OPENDCC_OS_WINDOWS
#include <Windows.h>
#elif defined(OPENDCC_OS_LINUX)

#elif defined(OPENDCC_OS_MAC)
#include <sys/sysctl.h>
#endif

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{

    bool is_debugged()
    {
#ifdef OPENDCC_OS_WINDOWS
        return IsDebuggerPresent();
#else
        // Not implemented
        return false;
#endif
    }

    void trap_debugger()
    {
#ifdef OPENDCC_OS_WINDOWS
        DebugBreak();
#else
        // Not implemented
#endif
    }

}

OPENDCC_NAMESPACE_CLOSE
