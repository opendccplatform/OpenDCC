// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/wrap_settings.h"
#include "opendcc/base/pybind_bridge/shiboken.h"
#include "opendcc/base/pybind_bridge/usd.h"
#include "opendcc/app/core/settings.h"
#include <opendcc/base/py_utils/error.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

using namespace pybind11;

static Settings::SettingChangedHandle register_setting_changed(Settings* self, const std::string& name, function callback_fn)
{
#define TRY_CALL(cpp_type)                                     \
    {                                                          \
        cpp_type actual_value;                                 \
        if (val.try_get<cpp_type>(&actual_value))              \
        {                                                      \
            callback_fn(path, actual_value, type);             \
            return;                                            \
        }                                                      \
    }                                                          \
    {                                                          \
        std::vector<cpp_type> actual_value;                    \
        if (val.try_get<std::vector<cpp_type>>(&actual_value)) \
        {                                                      \
            callback_fn(path, actual_value, type);             \
            return;                                            \
        }                                                      \
    }
    auto callback = [callback_fn](const std::string& path, const Settings::Value& val, Settings::ChangeType type) mutable {
        gil_scoped_acquire gil;
        try
        {
            TRY_CALL(bool);
            TRY_CALL(int64_t);
            TRY_CALL(double);
            TRY_CALL(std::string);
        }
        catch (const error_already_set& exc)
        {
            base::py_utils::py_log_error(exc.what());
        }
    };
#undef TRY_CALL
    return self->register_setting_changed(name, callback);
}

void py_interp::bind::wrap_settings(pybind11::module& m)
{
    enum_<Settings::ChangeType>(m, "ChangeType")
        .value("REMOVED", Settings::ChangeType::REMOVED)
        .value("RESET", Settings::ChangeType::RESET)
        .value("UPDATED", Settings::ChangeType::UPDATED);

    class_<Settings::SettingChangedHandle>(m, "SettingChangedHandle");
    class_<Settings>(m, "Settings")
        .def(init<>())
        .def(init<const std::string&>())
        .def("has", &Settings::has)
        .def("register_setting_changed", &register_setting_changed)
        .def("unregister_setting_changed", &Settings::unregister_setting_changed)
        .def("remove", &Settings::remove)
        .def("reset", &Settings::reset)
        .def_static("get_separator", &Settings::get_separator)
        .def("get_bool", &Settings::get<bool>, "path"_a, "fallback_value"_a = false)
        .def("get_bool_array", &Settings::get<std::vector<bool>>, "path"_a, "fallback_value"_a = std::vector<bool>())
        .def("get_int", &Settings::get<int64_t>, "path"_a, "fallback_value"_a = 0ll)
        .def("get_int_array", &Settings::get<std::vector<int64_t>>, "path"_a, "fallback_value"_a = std::vector<int64_t>())
        .def("get_double", &Settings::get<double>, "path"_a, "fallback_value"_a = 0.0)
        .def("get_double_array", &Settings::get<std::vector<double>>, "path"_a, "fallback_value"_a = std::vector<double>())
        .def("get_string", &Settings::get<std::string>, "path"_a, "fallback_value"_a = std::string())
        .def("get_string_array", &Settings::get<std::vector<std::string>>, "path"_a, "fallback_value"_a = std::vector<std::string>())
        .def("get_default_bool", &Settings::get_default<bool>, "path"_a, "fallback_value"_a = false)
        .def("get_default_int", &Settings::get_default<int64_t>, "path"_a, "fallback_value"_a = 0ll)
        .def("get_default_double", &Settings::get_default<double>, "path"_a, "fallback_value"_a = 0.0)
        .def("get_default_string", &Settings::get_default<std::string>, "path"_a, "fallback_value"_a = std::string())
        .def("get_default_bool_array", &Settings::get_default<std::vector<bool>>, "path"_a, "fallback_value"_a = std::vector<bool>())
        .def("get_default_int_array", &Settings::get_default<std::vector<int64_t>>, "path"_a, "fallback_value"_a = std::vector<int64_t>())
        .def("get_default_double_array", &Settings::get_default<std::vector<double>>, "path"_a, "fallback_value"_a = std::vector<double>())
        .def("get_default_string_array", &Settings::get_default<std::vector<std::string>>, "path"_a, "fallback_value"_a = std::vector<std::string>())
        .def("set_bool", &Settings::set<bool>)
        .def("set_bool_array", &Settings::set<std::vector<bool>>)
        .def("set_int", &Settings::set<int64_t>)
        .def("set_int_array", &Settings::set<std::vector<int64_t>>)
        .def("set_double", &Settings::set<double>)
        .def("set_double_array", &Settings::set<std::vector<double>>)
        .def("set_string", &Settings::set<std::string>)
        .def("set_string_array", &Settings::set<std::vector<std::string>>)
        .def("set_default_bool", &Settings::set_default<bool>)
        .def("set_default_int", &Settings::set_default<int64_t>)
        .def("set_default_double", &Settings::set_default<double>)
        .def("set_default_string", &Settings::set_default<std::string>)
        .def("set_default_bool_array", &Settings::set_default<std::vector<bool>>)
        .def("set_default_int_array", &Settings::set_default<std::vector<int64_t>>)
        .def("set_default_double_array", &Settings::set_default<std::vector<double>>)
        .def("set_default_string_array", &Settings::set_default<std::vector<std::string>>);
}
OPENDCC_NAMESPACE_CLOSE
