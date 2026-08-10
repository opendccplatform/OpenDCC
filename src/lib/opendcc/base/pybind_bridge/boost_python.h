/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pxr/pxr.h>

// Single include point for the boost.python flavor that matches the USD build.
// Spell the namespace as PXR_BOOST_PYTHON_NAMESPACE everywhere; it resolves to
// pxr::boost::python (or boost::python for USD builds that still use real boost).
#if PXR_VERSION >= 2411
#include <pxr/external/boost/python.hpp>
#else
#include <boost/python.hpp>
#ifndef PXR_BOOST_PYTHON_NAMESPACE
#define PXR_BOOST_PYTHON_NAMESPACE boost::python
#endif
#endif
