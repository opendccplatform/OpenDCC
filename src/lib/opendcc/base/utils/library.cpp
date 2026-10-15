// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/utils/library.h"
#if defined(OPENDCC_OS_WINDOWS)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#include <cerrno>

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{

    void* dl_open(const std::string& filename, int flags)
    {
#if defined(OPENDCC_OS_WINDOWS)
        return LoadLibraryEx(filename.c_str(), nullptr, flags);
#else
        (void)dlerror();
        return dlopen(filename.c_str(), flags);
#endif
    }

    int dl_close(void* handle)
    {
#if defined(OPENDCC_OS_WINDOWS)
        // dlclose() returns 0 on success and non-zero on error, the opposite of
        // FreeLibrary().
        int status = ::FreeLibrary(reinterpret_cast<HMODULE>(handle)) ? 0 : -1;
#else
        int status = dlclose(handle);
#endif
        return status;
    }

    void* dl_sym(void* handle, const char* name)
    {
#if defined(OPENDCC_OS_WINDOWS)
        return GetProcAddress(reinterpret_cast<HMODULE>(handle), name);
#else
        return dlsym(handle, name);
#endif
    }

    OPENDCC_BASE_UTILS_API std::string dl_error_str()
    {
#ifdef OPENDCC_OS_WINDOWS
        if (auto error = GetLastError())
        {
            LPSTR buffer = nullptr;
            const auto len = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                           error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer, 0, nullptr);

            std::string error_msg(buffer, len);
            LocalFree(buffer);
            return error_msg;
        }
        return std::string();
#else
        const char* const error = dlerror();
        return error ? std::string(error) : std::string();
#endif
    }

    OPENDCC_BASE_UTILS_API int dl_error()
    {
#ifdef OPENDCC_OS_WINDOWS
        return GetLastError();
#else
        return errno;
#endif
    }

    void* get_dl_handle(const std::string& library_name)
    {
#if defined(OPENDCC_OS_WINDOWS)
        return GetModuleHandle(library_name.c_str());
#else
        (void)dlerror();
        return dlopen(library_name.c_str(), RTLD_LAZY | RTLD_NOLOAD);
#endif
    }

}

OPENDCC_NAMESPACE_CLOSE
