/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "opendcc/opendcc.h"
#include <shiboken.h>

OPENDCC_NAMESPACE_OPEN

namespace base::pybind_bridge
{
    /** @brief A strong Python reference that acquires the GIL when released. */
    class PythonReference
    {
    public:
        explicit PythonReference(PyObject* object)
            : m_object(object)
        {
            Py_INCREF(m_object);
        }
        PythonReference(const PythonReference&) = delete;
        PythonReference& operator=(const PythonReference&) = delete;
        ~PythonReference()
        {
            if (Py_IsInitialized())
            {
                Shiboken::GilState gil;
                Py_DECREF(m_object);
            }
        }
        PyObject* get() const { return m_object; }

    private:
        PyObject* m_object;
    };
}

OPENDCC_NAMESPACE_CLOSE
