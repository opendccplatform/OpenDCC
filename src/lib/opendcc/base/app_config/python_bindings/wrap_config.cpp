// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <opendcc/base/app_config/config.h>
#include <codecvt>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

OPENDCC_NAMESPACE_OPEN

namespace base::app_config
{
    using namespace pybind11;

    namespace
    {
        std::wstring get_string(ApplicationConfig* config, const std::string& key, std::wstring _default = L"")
        {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            const std::string default_str = converter.to_bytes(_default);
            const auto ascii_str = config->get<std::string>(key, default_str);
            const auto unicode_str = converter.from_bytes(ascii_str);
            return unicode_str;
        }

        std::vector<std::wstring> get_string_array(ApplicationConfig* config, const std::string& key, std::vector<std::wstring> _default = {})
        {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            std::vector<std::string> default_strs;
            default_strs.reserve(_default.size());
            for (const auto& s : _default)
                default_strs.emplace_back(converter.to_bytes(s));
            const auto ascii_strs = config->get_array<std::string>(key, default_strs);
            std::vector<std::wstring> result;
            result.reserve(ascii_strs.size());
            for (const auto& s : ascii_strs)
                result.emplace_back(converter.from_bytes(s));
            return result;
        }
    };

    PYBIND11_MODULE(app_config, m)
    {
        class_<ApplicationConfig>(m, "ApplicationConfig")
            .def(init<>())
            .def(init<const std::string&>(), arg("filename"))
            .def("get_bool", &ApplicationConfig::get<bool>, arg("key"), arg("default") = false)
            .def("get_int", &ApplicationConfig::get<int>, arg("key"), arg("default") = 0)
            .def("get_double", &ApplicationConfig::get<double>, arg("key"), arg("default") = 0.0)
            .def("get_string", &get_string, arg("key"), arg("default") = std::wstring())
            .def("get_int_array", &ApplicationConfig::get_array<int64_t>, arg("key"), arg("default") = std::vector<int64_t>())
            .def("get_double_array", &ApplicationConfig::get_array<double>, arg("key"), arg("default") = std::vector<double>())
            .def("get_string_array", &get_string_array, arg("key"), arg("default") = std::vector<std::wstring>());
    }

}

OPENDCC_NAMESPACE_CLOSE
