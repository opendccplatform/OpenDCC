/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/base/py_utils/api.h"
#include "opendcc/base/logging/logger.h"
#include "Python.h"

OPENDCC_NAMESPACE_OPEN

class PyLock
{
public:
    PyLock() { m_state = PyGILState_Ensure(); }
    PyLock(const PyLock&) = delete;
    PyLock(PyLock&&) = delete;
    PyLock& operator=(const PyLock&) = delete;
    PyLock& operator=(PyLock&&) = delete;
    ~PyLock() { PyGILState_Release(m_state); }

private:
    PyGILState_STATE m_state;
};

OPENDCC_NAMESPACE_CLOSE
