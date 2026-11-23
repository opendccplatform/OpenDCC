/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/pybind_bridge/boost_python.h"

OPENDCC_NAMESPACE_OPEN

namespace base::pybind_bridge
{
    // All conversions require the GIL and use the converter registry of USD's Python runtime.
    /** @brief Borrow a Python object through the Boost.Python runtime used by USD. */
    inline PXR_BOOST_PYTHON_NAMESPACE::object borrowed_python_object(PyObject* value)
    {
        return PXR_BOOST_PYTHON_NAMESPACE::object(PXR_BOOST_PYTHON_NAMESPACE::handle<>(PXR_BOOST_PYTHON_NAMESPACE::borrowed(value)));
    }

    /** @brief Check whether the registered Boost.Python converters accept a value. */
    template <class T>
    bool is_python_convertible(PyObject* value)
    {
        return PXR_BOOST_PYTHON_NAMESPACE::extract<T>(borrowed_python_object(value)).check();
    }

    /** @brief Extract a value accepted by is_python_convertible<T>(). */
    template <class T>
    T from_python(PyObject* value)
    {
        return PXR_BOOST_PYTHON_NAMESPACE::extract<T>(borrowed_python_object(value))();
    }

    /** @brief Extract a value, leaving the output unchanged if conversion is unavailable. */
    template <class T>
    bool try_from_python(PyObject* value, T& out)
    {
        PXR_BOOST_PYTHON_NAMESPACE::extract<T> extractor(borrowed_python_object(value));
        if (!extractor.check())
            return false;
        out = extractor();
        return true;
    }

    /** @brief Convert a C++ value to a new Python reference. The caller holds the GIL. */
    template <class T>
    PyObject* to_python(const T& value)
    {
        PXR_BOOST_PYTHON_NAMESPACE::object result(value);
        return PXR_BOOST_PYTHON_NAMESPACE::incref(result.ptr());
    }
}

OPENDCC_NAMESPACE_CLOSE
