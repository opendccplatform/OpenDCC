/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/pybind_bridge/pybind11.h"
#include "opendcc/base/pybind_bridge/boost_python_conversions.h"
#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>

#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

DOCTEST_TEST_CASE("Boost.Python bridge uses registered USD value converters without retaining borrowed objects" *
                  doctest::test_suite("UsdOutlinerBindings"))
{
    pybind11::gil_scoped_acquire gil;
    pybind11::module_::import("pxr.Sdf");
    pybind11::module_::import("pxr.Tf");
    using namespace base::pybind_bridge;

    const SdfPath path("/Root");
    auto python_path = pybind11::reinterpret_steal<pybind11::object>(to_python(path));
    const auto references = Py_REFCNT(python_path.ptr());
    DOCTEST_REQUIRE(is_python_convertible<SdfPath>(python_path.ptr()));
    DOCTEST_CHECK(from_python<SdfPath>(python_path.ptr()) == path);
    DOCTEST_CHECK(Py_REFCNT(python_path.ptr()) == references);

    const TfToken token("registered_token");
    auto python_token = pybind11::reinterpret_steal<pybind11::object>(to_python(token));
    DOCTEST_CHECK(from_python<TfToken>(python_token.ptr()) == token);

    auto invalid = pybind11::module_::import("builtins").attr("object")();
    SdfPath unchanged("/Unchanged");
    DOCTEST_CHECK_FALSE(is_python_convertible<SdfPath>(invalid.ptr()));
    DOCTEST_CHECK_FALSE(try_from_python(invalid.ptr(), unchanged));
    DOCTEST_CHECK(unchanged == SdfPath("/Unchanged"));
    DOCTEST_CHECK_FALSE(PyErr_Occurred());
    bool rejected = false;
    try
    {
        from_python<SdfPath>(invalid.ptr());
    }
    catch (const PXR_BOOST_PYTHON_NAMESPACE::error_already_set&)
    {
        rejected = true;
        DOCTEST_CHECK(PyErr_ExceptionMatches(PyExc_TypeError));
        PyErr_Clear();
    }
    DOCTEST_CHECK(rejected);
}

DOCTEST_TEST_CASE("Boost.Python bridge preserves stage references and null values" * doctest::test_suite("UsdOutlinerBindings"))
{
    pybind11::gil_scoped_acquire gil;
    pybind11::module_::import("pxr.Usd");
    using namespace base::pybind_bridge;

    auto stage = UsdStage::CreateInMemory();
    auto python_stage = pybind11::reinterpret_steal<pybind11::object>(to_python(stage));
    auto retained = from_python<UsdStageRefPtr>(python_stage.ptr());
    DOCTEST_REQUIRE(retained);
    DOCTEST_CHECK(retained->GetRootLayer());
    DOCTEST_CHECK(try_from_python(python_stage.ptr(), stage));
    DOCTEST_CHECK(stage == retained);

    auto empty = pybind11::reinterpret_steal<pybind11::object>(to_python(UsdStageRefPtr()));
    DOCTEST_CHECK(empty.is_none());
    DOCTEST_CHECK_FALSE(from_python<UsdStageRefPtr>(empty.ptr()));
}

OPENDCC_NAMESPACE_CLOSE
