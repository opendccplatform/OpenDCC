/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>
#include <pybind11/operators.h>
#include <pybind11/chrono.h>
#include <pybind11/embed.h>
#include <pybind11/cast.h>
#include "opendcc/base/logging/logger.h"
#include "opendcc/base/py_utils/error.h"

OPENDCC_NAMESPACE_OPEN

namespace base::pybind_bridge
{

    template <class Return, class... Args>
    struct pybind_safe_callback
    {
        pybind_safe_callback(const std::function<Return(Args...)>& func)
            : function(func)
        {
        }

        Return operator()(Args... args) const
        {
            try
            {
                return function(std::forward<Args>(args)...);
            }
            catch (const pybind11::error_already_set& exc)
            {
                base::py_utils::py_log_error(exc.what());
                if constexpr (!std::is_void_v<Return>)
                {
                    return Return();
                }
            }
        }

        std::function<Return(Args...)> function;
    };

    template <typename Return, typename... Args>
    pybind_safe_callback(std::function<Return(Args...)>) -> pybind_safe_callback<Return, Args...>;

#define OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(ret_type, cname, fn, ...) \
    do                                                                     \
    {                                                                      \
        pybind11::gil_scoped_acquire gil;                                  \
        try                                                                \
        {                                                                  \
            PYBIND11_OVERRIDE(ret_type, cname, fn, __VA_ARGS__);           \
        }                                                                  \
        catch (const pybind11::error_already_set& exc)                     \
        {                                                                  \
            OPENDCC_NAMESPACE::base::py_utils::py_log_error(exc.what());   \
        }                                                                  \
        return cname::fn(__VA_ARGS__);                                     \
    } while (false)

#define OPENDCC_PYBIND11_OVERRIDE_PURE_EXCEPTION_SAFE(ret_type, cname, fn, ...)                                    \
    do                                                                                                             \
    {                                                                                                              \
        pybind11::gil_scoped_acquire gil;                                                                          \
        try                                                                                                        \
        {                                                                                                          \
            PYBIND11_OVERRIDE_PURE(ret_type, cname, fn, __VA_ARGS__);                                              \
        }                                                                                                          \
        catch (const pybind11::error_already_set& exc)                                                             \
        {                                                                                                          \
            OPENDCC_NAMESPACE::base::py_utils::py_log_error(exc.what());                                           \
        }                                                                                                          \
        pybind11::pybind11_fail("Tried to call pure virtual function \"" PYBIND11_STRINGIFY(cname) "::" #fn "\""); \
    } while (false)

}

OPENDCC_NAMESPACE_CLOSE
