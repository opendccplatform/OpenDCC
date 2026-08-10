/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/base/pybind_bridge/pybind11.h"
#include "opendcc/base/pybind_bridge/boost_python.h"

OPENDCC_NAMESPACE_OPEN

template <class T>
struct pybind_boost_bridge
{
    static bool load(T& value, const pybind11::handle& src)
    {
        namespace bp = PXR_BOOST_PYTHON_NAMESPACE;
        namespace py = pybind11;
        bp::object boost_obj { bp::handle<>(bp::borrowed(src.ptr())) };
        bp::extract<T> extractor(boost_obj);
        if (extractor.check())
        {
            value = extractor();
            return true;
        }
        return false;
    }

    // C++ -> Python
    static pybind11::handle cast(const T& src)
    {
        namespace bp = PXR_BOOST_PYTHON_NAMESPACE;
        return bp::incref(bp::object(src).ptr());
    }
};

OPENDCC_NAMESPACE_CLOSE

#define OPENDCC_PYBIND_BOOST_BRIDGE(cpp_type, python_type_name)                                          \
    namespace pybind11                                                                                   \
    {                                                                                                    \
        namespace detail                                                                                 \
        {                                                                                                \
            template <>                                                                                  \
            struct type_caster<cpp_type>                                                                 \
            {                                                                                            \
                using underlying_type = cpp_type;                                                        \
                PYBIND11_TYPE_CASTER(underlying_type, _(python_type_name));                              \
                bool load(pybind11::handle src, bool)                                                    \
                {                                                                                        \
                    return OPENDCC_NAMESPACE::pybind_boost_bridge<underlying_type>::load(value, src);    \
                }                                                                                        \
                                                                                                         \
                static handle cast(underlying_type src, pybind11::return_value_policy, pybind11::handle) \
                {                                                                                        \
                    return OPENDCC_NAMESPACE::pybind_boost_bridge<underlying_type>::cast(src);           \
                }                                                                                        \
            };                                                                                           \
        }                                                                                                \
    }
