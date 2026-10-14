/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/packaging/package.h"
#include "opendcc/base/packaging/package_registry.h"
#include "opendcc/base/packaging/filesystem_package_provider.h"
#include "opendcc/base/pybind_bridge/pybind11.h"
#include "package_test_utils.h"
#include <cstdint>
#include "opendcc/base/utils/scope_guard.h"

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

DOCTEST_TEST_SUITE("PackagingValues")
{
    DOCTEST_TEST_CASE("dotted_lookup_and_owned_defaults")
    {
        PackageSharedData data;
        data.name = "values";
        DOCTEST_REQUIRE(set_package_attribute(data.resolved_attributes, "nested.name", std::string("value")));
        DOCTEST_REQUIRE(set_package_attribute(data.resolved_attributes, "nested.count", int64_t { 42 }));
        data.resolved_attributes["literal.key"] = std::string("literal");
        data.resolved_attributes["empty"] = std::any();
        const auto empty = find_package_attribute(data.resolved_attributes, "empty");
        DOCTEST_REQUIRE(empty);
        DOCTEST_CHECK_FALSE(empty->get().has_value());
        DOCTEST_CHECK(&empty->get() == &data.resolved_attributes.at("empty"));
        DOCTEST_CHECK(data.get_resolved("nested.name", "fallback") == "value");
        DOCTEST_CHECK(data.get_resolved(".nested..name.", "fallback") == "value");
        DOCTEST_CHECK_FALSE(find_package_attribute(data.resolved_attributes, ""));
        const auto missing = find_package_attribute(data.resolved_attributes, "...");
        DOCTEST_REQUIRE_FALSE(missing);
        DOCTEST_CHECK(missing.error() == PackageValueError::NOT_FOUND);
        DOCTEST_REQUIRE(set_package_attribute(data.resolved_attributes, ".nested..enabled.", true));
        DOCTEST_CHECK(data.get_resolved("nested.enabled", false));
        const auto attribute_count = data.resolved_attributes.size();
        DOCTEST_REQUIRE(set_package_attribute(data.resolved_attributes, "...", true));
        DOCTEST_CHECK(data.resolved_attributes.size() == attribute_count);
        DOCTEST_CHECK(data.get_resolved("missing", "fallback") == "fallback");
        DOCTEST_CHECK(data.get_resolved("nested.count", "fallback") == "fallback");
        DOCTEST_CHECK_FALSE(find_package_attribute(data.resolved_attributes, "nested.name.child"));
        DOCTEST_CHECK_FALSE(find_package_attribute(data.resolved_attributes, "literal.key"));
        DOCTEST_CHECK(std::any_cast<std::string>(data.resolved_attributes.at("literal.key")) == "literal");
        DOCTEST_CHECK(data.get_resolved<PackageAnyArray>("missing").empty());
        const auto copied_default = data.get_resolved<PackageAnyArray>("missing", PackageAnyArray { std::string("owned") });
        DOCTEST_CHECK(std::any_cast<std::string>(copied_default.front()) == "owned");
        DOCTEST_REQUIRE(set_package_attribute(data.resolved_attributes, "nested.count.child", false));
        DOCTEST_CHECK_FALSE(data.get_resolved("nested.count.child", true));
        const auto rejected = set_package_attribute(data.resolved_attributes, "nested.count", std::vector<int> { 1 });
        DOCTEST_REQUIRE_FALSE(rejected);
        DOCTEST_CHECK(rejected.error() == PackageValueError::UNSUPPORTED_TYPE);
        DOCTEST_CHECK_FALSE(data.get_resolved("nested.count.child", true));
    }

    DOCTEST_TEST_CASE("malformed_required_attributes_reject_package")
    {
        packaging_tests::TemporaryDirectory directory;
        directory.write("root/package.toml", "[base]\nname = 'root'\nroot = 42\n");
        directory.write("dependencies/package.toml", "dependencies = false\n[base]\nname = 'dependencies'\n");
        PackageRegistry registry;
        auto provider = std::make_shared<FileSystemPackageProvider>();
        provider->add_path((directory.path() / "*").generic_u8string().c_str());
        registry.add_package_provider(provider);
        registry.fetch_packages();
        DOCTEST_CHECK(registry.get_all_packages().empty());
    }

    DOCTEST_TEST_CASE("non_dictionary_loader_entries_are_skipped")
    {
        namespace py = pybind11;
        py::gil_scoped_acquire gil;
        auto sys = py::module_::import("sys");
        const py::list saved_path(sys.attr("path"));
        const ScopeGuard restore_path([&] { sys.attr("path") = saved_path; });
        packaging_tests::TemporaryDirectory directory;
        auto data = std::make_shared<PackageSharedData>();
        data->name = "invalid_entries";
        data->root_dir = directory.path().generic_u8string();
        const PackageAnyArray invalid_entries { false, std::string("invalid"), int64_t { 42 } };
        DOCTEST_REQUIRE(set_package_attribute(data->resolved_attributes, "native.load", invalid_entries));
        DOCTEST_REQUIRE(set_package_attribute(data->resolved_attributes, "python.import", invalid_entries));
        DOCTEST_REQUIRE(set_package_attribute(data->resolved_attributes, "python.path", invalid_entries));
        DOCTEST_REQUIRE(set_package_attribute(data->resolved_attributes, "environment.OPENDCC_INVALID_ENTRIES", invalid_entries));
        std::unordered_map<std::string, std::shared_ptr<PackageSharedData>> packages { { data->name, data } };
        auto resolver = std::make_shared<PackageResolver>();
        resolver->set_packages(packages);
        PackageLoader loader(resolver, packages);
        DOCTEST_CHECK(loader.load(data->name));
        DOCTEST_CHECK(data->loaded);
        DOCTEST_CHECK(data->loaded_entities.empty());
        DOCTEST_CHECK(loader.unload(data->name));
    }

#ifndef OPENDCC_OS_WINDOWS
    DOCTEST_TEST_CASE("discovery_retains_packages_after_filesystem_errors")
    {
        packaging_tests::TemporaryDirectory directory;
        directory.write("valid/package.toml", "[base]\nname = 'partial_discovery'\n");
        const auto blocked = directory.path() / "blocked";
        std::filesystem::create_directory(blocked);
        const ScopeGuard restore([&] { std::filesystem::permissions(blocked, std::filesystem::perms::owner_all); });
        std::filesystem::permissions(blocked, std::filesystem::perms::none);
        std::error_code error;
        std::filesystem::directory_iterator inaccessible(blocked, error);
        if (!error)
            return;
        FileSystemPackageProvider provider;
        provider.add_path((blocked / "*").generic_u8string().c_str());
        provider.add_path((directory.path() / "valid").generic_u8string().c_str());
        provider.fetch();
        DOCTEST_REQUIRE(provider.get_cached_packages().size() == 1);
        DOCTEST_CHECK(provider.get_cached_packages().front().name == "partial_discovery");
    }
#endif

    DOCTEST_TEST_CASE("unsupported_cpp_value_rejected_by_python")
    {
        namespace py = pybind11;
        py::gil_scoped_acquire gil;
        py::module_::import("opendcc.packaging");
        PackageAttribute attribute { "unsupported", std::vector<int> { 1 } };
        const auto object = py::cast(&attribute, py::return_value_policy::reference);
        bool rejected = false;
        try
        {
            const py::object value = object.attr("value");
        }
        catch (const py::error_already_set& error)
        {
            rejected = error.matches(PyExc_TypeError);
        }
        DOCTEST_CHECK(rejected);
    }
}

OPENDCC_NAMESPACE_CLOSE
