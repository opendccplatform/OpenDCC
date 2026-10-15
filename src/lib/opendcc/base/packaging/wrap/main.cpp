// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/package_entry_point.h"
#include "opendcc/base/pybind_bridge/pybind11.h"
#include <cstdint>
#include "opendcc/base/utils/scope_guard.h"
#include "opendcc/base/packaging/package_registry.h"
#include "opendcc/base/packaging/filesystem_package_provider.h"
#include "opendcc/base/packaging/toml_parser.h"

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    using namespace pybind11;

    namespace
    {
        object value_to_python(const std::any& value);

        dict dictionary_to_python(const PackageAnyDictionary& values)
        {
            dict result;
            for (const auto& item : values)
                result[str(item.first)] = value_to_python(item.second);
            return result;
        }

        object value_to_python(const std::any& value)
        {
            if (!value.has_value())
                return none();
            if (const auto item = std::any_cast<std::string>(&value))
                return str(*item);
            if (const auto item = std::any_cast<bool>(&value))
                return bool_(*item);
            if (const auto item = std::any_cast<int64_t>(&value))
                return int_(*item);
            if (const auto item = std::any_cast<double>(&value))
                return float_(*item);
            if (const auto items = std::any_cast<PackageAnyDictionary>(&value))
            {
                return dictionary_to_python(*items);
            }
            if (const auto items = std::any_cast<PackageAnyArray>(&value))
            {
                list result;
                for (const auto& item : *items)
                    result.append(value_to_python(item));
                return result;
            }
            throw type_error("Unsupported packaging value type");
        }

        std::any value_from_python(handle value)
        {
            if (value.is_none())
                return {};
            if (isinstance<bool_>(value))
                return value.cast<bool>();
            if (isinstance<int_>(value))
            {
                const auto result = PyLong_AsLongLong(value.ptr());
                if (PyErr_Occurred())
                    throw error_already_set();
                return static_cast<int64_t>(result);
            }
            if (isinstance<float_>(value))
                return value.cast<double>();
            if (isinstance<str>(value))
                return value.cast<std::string>();
            const bool is_dictionary = isinstance<dict>(value);
            if (!is_dictionary && !isinstance<list>(value))
                throw type_error("Unsupported packaging value type");

            if (Py_EnterRecursiveCall(" while converting a packaging value"))
                throw error_already_set();
            const utils::ScopeGuard leave([] { Py_LeaveRecursiveCall(); });
            if (is_dictionary)
            {
                PackageAnyDictionary result;
                for (const auto& item : reinterpret_borrow<dict>(value))
                {
                    if (!isinstance<str>(item.first))
                        throw type_error("Packaging dictionary keys must be strings");
                    result[item.first.cast<std::string>()] = value_from_python(item.second);
                }
                return std::any(std::move(result));
            }
            PackageAnyArray result;
            for (const auto& item : reinterpret_borrow<list>(value))
                result.push_back(value_from_python(item));
            return std::any(std::move(result));
        }

        std::any copy_value_for_python(const std::any& value)
        {
            auto copied = copy_package_value(value);
            if (!copied)
                throw type_error("Unsupported packaging value type");
            return std::move(*copied);
        }

        PackageAttributeList copy_attributes(const PackageAttributeList& attributes)
        {
            PackageAttributeList result;
            result.reserve(attributes.size());
            for (const auto& attribute : attributes)
                result.push_back({ attribute.name, copy_value_for_python(attribute.value) });
            return result;
        }

        PackageData copy_package_data(const PackageData& data)
        {
            return { data.name, data.path, copy_attributes(data.raw_attributes) };
        }

        PackageDataList cached_packages_to_python(const PackageProvider& provider)
        {
            PackageDataList result;
            for (const auto& data : provider.get_cached_packages())
                result.push_back(copy_package_data(data));
            return result;
        }

        PackageData parse_to_python(PackageParser& parser, const std::string& path)
        {
            return copy_package_data(parser.parse(path));
        }

        struct PackageEntryPointWrap : PackageEntryPoint
        {
            using PackageEntryPoint::PackageEntryPoint;

            virtual void initialize(const Package& package) override
            {
                OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(void, PackageEntryPoint, initialize, package);
            }
            virtual void uninitialize(const Package& package) override
            {
                OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(void, PackageEntryPoint, uninitialize, package);
            }
        };
    };

    PYBIND11_MODULE(PYMODULE_NAME, m)
    {
        class_<Package>(m, "Package")
            .def("get_name", &Package::get_name)
            .def("get_all_attributes", [](const Package& package) { return dictionary_to_python(package.get_all_attributes()); })
            .def("get_attribute", [](const Package& package, const std::string& name) { return value_to_python(package.get_attribute(name)); })
            .def("is_loaded", &Package::is_loaded)
            .def("get_direct_dependencies", [](const Package& package) { return dictionary_to_python(package.get_direct_dependencies()); })
            .def("get_root_dir", &Package::get_root_dir);

        class_<PackageEntryPoint, PackageEntryPointWrap>(m, "PackageEntryPoint")
            .def(init<>())
            .def("initialize", &PackageEntryPoint::initialize)
            .def("uninitialize", &PackageEntryPoint::uninitialize);

        class_<PackageRegistry, std::unique_ptr<PackageRegistry, nodelete>>(m, "PackageRegistry")
            .def("fetch_packages", &PackageRegistry::fetch_packages, "load_fetched"_a = false)
            .def("add_package_provider", &PackageRegistry::add_package_provider, "package_resolver"_a)
            .def("remove_package_provider", &PackageRegistry::remove_package_provider, "package_resolver"_a)
            .def("get_all_packages", &PackageRegistry::get_all_packages)
            .def("get_package", &PackageRegistry::get_package, "name"_a)
            .def("define_token", &PackageRegistry::define_token, "token_name"_a, "token_value"_a)
            .def("undefine_token", &PackageRegistry::undefine_token, "token_name"_a)
            .def("load", overload_cast<const std::string&>(&PackageRegistry::load), "pkg_name"_a)
            .def("load", overload_cast<const Package&>(&PackageRegistry::load), "pkg"_a);

        class_<PackageProvider, std::shared_ptr<PackageProvider>>(m, "PackageProvider")
            .def("fetch", &PackageProvider::fetch)
            .def("get_cached_packages", &cached_packages_to_python);

        class_<FileSystemPackageProvider, PackageProvider, std::shared_ptr<FileSystemPackageProvider>>(m, "FileSystemPackageProvider")
            .def(init<>())
            .def("fetch", &FileSystemPackageProvider::fetch)
            .def("get_cached_packages", &cached_packages_to_python)
            .def("register_package_parser", &FileSystemPackageProvider::register_package_parser)
            .def("add_path", &FileSystemPackageProvider::add_path)
            .def("remove_path", &FileSystemPackageProvider::remove_path);

        class_<PackageAttribute>(m, "PackageAttribute")
            .def_readwrite("name", &PackageAttribute::name)
            .def_property(
                "value", [](const PackageAttribute& attribute) { return value_to_python(attribute.value); },
                [](PackageAttribute& attribute, handle value) { attribute.value = copy_value_for_python(value_from_python(value)); });

        class_<PackageData>(m, "PackageData")
            .def_readwrite("name", &PackageData::name)
            .def_readwrite("path", &PackageData::path)
            .def_property(
                "raw_attributes", [](const PackageData& data) { return copy_attributes(data.raw_attributes); },
                [](PackageData& data, const PackageAttributeList& attributes) { data.raw_attributes = copy_attributes(attributes); });

        class_<PackageParser, std::shared_ptr<PackageParser>>(m, "PackageParser").def("parse", &parse_to_python);

        class_<TOMLParser, PackageParser, std::shared_ptr<TOMLParser>>(m, "TOMLParser").def(init<>()).def("parse", &parse_to_python);
    }

}

OPENDCC_NAMESPACE_CLOSE
