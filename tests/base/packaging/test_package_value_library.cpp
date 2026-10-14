/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/packaging/package_registry.h"
#include "opendcc/base/pybind_bridge/pybind11.h"
#include "opendcc/base/utils/library.h"
#include "opendcc/base/utils/scope_guard.h"
#ifndef OPENDCC_OS_WINDOWS
#include <dlfcn.h>
#endif

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

namespace
{
    struct ValueProvider : PackageProvider
    {
        std::vector<PackageData> packages;
        void fetch() override {}
        const std::vector<PackageData>& get_cached_packages() const override { return packages; }
    };

    struct ValueParser : PackageParser
    {
        PackageData data;
        PackageData parse(const std::string&) override { return data; }
    };
}

DOCTEST_TEST_SUITE("PackagingValueLibrary")
{
    DOCTEST_TEST_CASE("typed_access_and_unsupported_provider_values")
    {
        const auto text = copy_package_value(std::string("text"));
        DOCTEST_REQUIRE(text);
        DOCTEST_CHECK(std::any_cast<std::string>(*text) == "text");
        DOCTEST_CHECK(std::any_cast<bool>(&*text) == nullptr);
        DOCTEST_CHECK_THROWS_AS(static_cast<void>(std::any_cast<bool>(*text)), std::bad_any_cast);
        const auto empty = copy_package_value(std::any());
        DOCTEST_REQUIRE(empty);
        DOCTEST_CHECK_FALSE(empty->has_value());
        for (const auto& value : { std::any(std::vector<int> { 1 }), std::any(PackageAnyArray { std::vector<int> { 1 } }),
                                   std::any(PackageAnyDictionary { { "nested", std::vector<int> { 1 } } }) })
        {
            const auto copied = copy_package_value(value);
            DOCTEST_REQUIRE_FALSE(copied);
            DOCTEST_CHECK(copied.error() == PackageValueError::UNSUPPORTED_TYPE);
        }
        auto provider = std::make_shared<ValueProvider>();
        PackageData data;
        data.name = "unsupported_values";
        data.path = ".";
        data.raw_attributes.push_back({ "test", PackageAnyArray { std::vector<int> { 1 } } });
        provider->packages.push_back(std::move(data));
        PackageRegistry registry;
        registry.add_package_provider(provider);
        registry.fetch_packages();
        DOCTEST_CHECK(registry.get_all_packages().empty());
    }

    DOCTEST_TEST_CASE("native_and_python_values_survive_producer_unload")
    {
#ifdef OPENDCC_OS_WINDOWS
        auto library = dl_open(OPENDCC_PACKAGING_VALUE_LIBRARY, 0);
#else
        auto library = dl_open(OPENDCC_PACKAGING_VALUE_LIBRARY, RTLD_NOW | RTLD_LOCAL);
#endif
        DOCTEST_REQUIRE(library);
        const ScopeGuard close([&] {
            if (library)
                dl_close(library);
        });
        const auto produce = reinterpret_cast<void (*)(std::any*, bool)>(dl_sym(library, "produce_package_value"));
        const auto check = reinterpret_cast<bool (*)(const std::any*)>(dl_sym(library, "check_package_value"));
        DOCTEST_REQUIRE(produce);
        DOCTEST_REQUIRE(check);

        std::any owned;
        produce(&owned, true);
        DOCTEST_CHECK(check(&owned));
        namespace py = pybind11;
        py::gil_scoped_acquire gil;
        py::module_::import("opendcc.packaging");
        PackageAttribute attribute { "values", owned };
        auto python_attribute = py::cast(&attribute, py::return_value_policy::reference);
        py::object python_value = python_attribute.attr("value");
        python_attribute.attr("value") = python_value;
        DOCTEST_CHECK(check(&attribute.value));

        auto provider = std::make_shared<ValueProvider>();
        PackageData data;
        data.name = "foreign_values";
        data.path = ".";
        std::any foreign;
        produce(&foreign, false);
        data.raw_attributes.push_back({ "test", std::move(foreign) });
        provider->packages.push_back(std::move(data));
        auto wrapped_provider = py::cast(std::static_pointer_cast<PackageProvider>(provider));
        auto cached = wrapped_provider.attr("get_cached_packages")();
        auto parser = std::make_shared<ValueParser>();
        parser->data = provider->packages.front();
        auto wrapped_parser = py::cast(std::static_pointer_cast<PackageParser>(parser));
        auto parsed = wrapped_parser.attr("parse")("unused");
        py::object copied_attributes = py::cast(&provider->packages.front(), py::return_value_policy::reference).attr("raw_attributes");
        {
            const py::object raw_attribute = py::cast(&parser->data.raw_attributes.front(), py::return_value_policy::reference);
            parsed.attr("raw_attributes") = py::make_tuple(raw_attribute);
        }
        parser->data = {};
        PackageRegistry registry;
        registry.add_package_provider(provider);
        registry.fetch_packages();
        auto package = registry.get_package("foreign_values");
        DOCTEST_REQUIRE(package);
        DOCTEST_CHECK(check(&package.get_attribute("test")));
        provider->packages.clear();
        registry.remove_package_provider(provider);
        provider.reset();
        DOCTEST_CHECK(dl_close(library) == 0);
        library = nullptr;
        const auto remaining_handle = get_dl_handle(OPENDCC_PACKAGING_VALUE_LIBRARY);
#ifndef OPENDCC_OS_WINDOWS
        if (remaining_handle)
            dl_close(remaining_handle);
#endif
        DOCTEST_CHECK(remaining_handle == nullptr);

        const auto retained = copy_package_value(package.get_attribute("test"));
        DOCTEST_REQUIRE(retained);
        const auto dictionary = std::any_cast<PackageAnyDictionary>(&*retained);
        DOCTEST_REQUIRE(dictionary);
        const auto& values = std::any_cast<const PackageAnyArray&>(dictionary->at("nested"));
        DOCTEST_CHECK(std::any_cast<int64_t>(values.at(1)) == 42);
        DOCTEST_CHECK(std::any_cast<bool>(values.at(0)));
        DOCTEST_CHECK(std::any_cast<double>(values.at(2)) == 2.5);
        DOCTEST_CHECK(std::any_cast<std::string>(values.at(3)) == "text");
        DOCTEST_CHECK_FALSE(values.at(4).has_value());
        DOCTEST_CHECK(py::object(python_attribute.attr("value")).equal(python_value));
        const py::object cached_attributes = cached[py::int_(0)].attr("raw_attributes");
        DOCTEST_CHECK(py::object(cached_attributes[py::int_(0)].attr("value")).equal(python_value));
        DOCTEST_CHECK(py::object(parsed.attr("raw_attributes")[py::int_(0)].attr("value")).equal(python_value));
        DOCTEST_CHECK(py::object(copied_attributes[py::int_(0)].attr("value")).equal(python_value));
        DOCTEST_CHECK(copy_package_value(owned).has_value());
        owned.reset();
        attribute.value.reset();
    }
}

OPENDCC_NAMESPACE_CLOSE
