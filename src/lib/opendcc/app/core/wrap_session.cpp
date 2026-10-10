// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/wrap_session.h"
#include "opendcc/base/pybind_bridge/usd.h"
#include "opendcc/app/core/session.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE
void py_interp::bind::wrap_session(pybind11::module &m)
{
    using namespace pybind11;
    class_<Session, std::unique_ptr<Session, nodelete>>(m, "Session")
        .def("get_stage_cache",
             [](Session *self) {
                 namespace bp = PXR_BOOST_PYTHON_NAMESPACE;
                 bp::object cache(bp::ptr(&self->get_stage_cache()));
                 return reinterpret_borrow<object>(cache.ptr());
             })
        .def("set_current_stage", overload_cast<UsdStageCache::Id>(&Session::set_current_stage), arg("id"))
        .def("set_current_stage", overload_cast<const UsdStageRefPtr &>(&Session::set_current_stage), arg("stage"))
        .def("get_stage_id", &Session::get_stage_id)
        .def("get_current_stage_id", &Session::get_current_stage_id)
        .def("open_stage", overload_cast<const SdfLayerHandle &>(&Session::open_stage))
        .def("open_stage", overload_cast<const std::string &>(&Session::open_stage))
        .def("close_stage", (bool(Session::*)(UsdStageCache::Id)) & Session::close_stage, arg("id"))
        .def("close_stage", (bool(Session::*)(const UsdStageRefPtr &)) & Session::close_stage, arg("stage"))
        .def("close_all", &Session::close_all)
        .def("force_update_stage_list", &Session::force_update_stage_list)
        .def("get_current_stage", &Session::get_current_stage)
        .def("get_stage_list", &Session::get_stage_list)
        .def("get_stage_bbox_cache", &Session::get_stage_bbox_cache, return_value_policy::reference)
        .def("get_stage_xform_cache", &Session::get_stage_xform_cache, return_value_policy::reference)
        .def("enable_live_sharing", &Session::enable_live_sharing)
        .def("is_live_sharing_enabled", &Session::is_live_sharing_enabled);
}
OPENDCC_NAMESPACE_CLOSE
