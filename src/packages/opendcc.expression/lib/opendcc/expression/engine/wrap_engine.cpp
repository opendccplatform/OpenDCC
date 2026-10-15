// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/opendcc.h"
#include "opendcc/base/pybind_bridge/usd.h"
#include "engine.h"
#include "session.h"

OPENDCC_NAMESPACE_OPEN
namespace pybind_bridge = base::pybind_bridge;

using namespace pybind11;

namespace
{
    ExpressionSession& expression_session()
    {
        return ExpressionSession::instance();
    }
}

PYBIND11_MODULE(expression_engine, m)
{
    class_<ExpressionEngine, std::shared_ptr<ExpressionEngine>>(m, "ExpressionEngine")
        .def("set_expression",
             (bool(ExpressionEngine::*)(PXR_NS::UsdAttribute, PXR_NS::TfToken, std::string, bool)) & ExpressionEngine::set_expression)
        .def("remove_expression", (bool(ExpressionEngine::*)(PXR_NS::SdfPath)) & ExpressionEngine::remove_expression)
        .def("remove_expression", (bool(ExpressionEngine::*)(PXR_NS::UsdAttribute)) & ExpressionEngine::remove_expression)
        .def("has_expression", &ExpressionEngine::has_expression)
        .def("set_variable", &ExpressionEngine::set_variable)
        .def("get_variable", &ExpressionEngine::get_variable)
        .def("get_variables_list", &ExpressionEngine::get_variables_list)
        .def("erase_variable", &ExpressionEngine::erase_variable)
        .def("bake", &ExpressionEngine::bake)
        .def("bake_all", &ExpressionEngine::bake_all)
        .def_static("invalid_callback_id", &ExpressionEngine::invalid_callback_id)
        .def("register_expression_callback",
             [](ExpressionEngine* self, PXR_NS::SdfPath attr_path, std::function<void(PXR_NS::SdfPath, const PXR_NS::VtValue&)> cb) {
                 return self->register_expression_callback(attr_path, pybind_bridge::pybind_safe_callback(cb));
             })
        .def("register_variable_changed_callback",
             [](ExpressionEngine* self, std::string variables_name,
                std::function<void(const std::string& variable_name, const PXR_NS::VtValue& value)> cb) {
                 return self->register_variable_changed_callback(variables_name, pybind_bridge::pybind_safe_callback(cb));
             })
        .def("evaluate", &ExpressionEngine::evaluate, arg("attr_path"), arg("time") = PXR_NS::UsdTimeCode::Default())
        .def("unregister_callback", &ExpressionEngine::unregister_callback);

    m.def("session", &expression_session, return_value_policy::reference);

    class_<ExpressionSession, std::unique_ptr<ExpressionSession, nodelete>>(m, "ExpressionSession")
        .def("current_engine", &ExpressionSession::current_engine)
        .def("need_skip", &ExpressionSession::need_skip);
}
OPENDCC_NAMESPACE_CLOSE
