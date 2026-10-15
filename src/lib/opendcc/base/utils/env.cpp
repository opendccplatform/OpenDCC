// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/utils/env.h"
#include "opendcc/base/defines.h"
#include <memory>

#if defined(OPENDCC_OS_WINDOWS)
#include <Windows.h>
#else
#include <stdlib.h>
#endif

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{

    std::string get_env(const std::string_view& name)
    {
#if defined(OPENDCC_OS_WINDOWS)
        const DWORD size = GetEnvironmentVariable(name.data(), nullptr, 0);
        if (size != 0)
        {
            std::unique_ptr<char[]> buffer(new char[size]);
            GetEnvironmentVariable(name.data(), buffer.get(), size);
            return std::string(buffer.get());
        }
#else
        if (const char* const result = getenv(name.data()))
        {
            return std::string(result);
        }
#endif

        return std::string();
    }

    bool set_env(const std::string_view& name, const std::string_view& value)
    {
#if defined(OPENDCC_OS_WINDOWS)
        return SetEnvironmentVariable(name.data(), value.data()) != 0;
#else
        return setenv(name.data(), value.data(), 1) == 0;
#endif
    }

}

OPENDCC_NAMESPACE_CLOSE
