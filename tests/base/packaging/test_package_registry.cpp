/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/pybind_bridge/pybind11.h"
#include "opendcc/base/packaging/package_registry.h"
#include "opendcc/base/packaging/filesystem_package_provider.h"
#include "opendcc/base/packaging/toml_parser.h"
#include "opendcc/base/packaging/package_resolver.h"
#include "opendcc/base/vendor/spdlog/fmt/fmt.h"
#include "package_test_utils.h"
#include <algorithm>

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

namespace
{
    struct RegistryFixture
    {
        // Python members must be destroyed before the GIL is released.
        pybind11::gil_scoped_acquire gil;
        packaging_tests::TemporaryDirectory directory;
        pybind11::module_ sys = pybind11::module_::import("sys");
        pybind11::list saved_path = pybind11::list(sys.attr("path"));
        PackageRegistry registry;
        std::shared_ptr<FileSystemPackageProvider> provider = std::make_shared<FileSystemPackageProvider>();

        RegistryFixture()
        {
            provider->add_path((directory.path() / "*").generic_u8string().c_str());
            registry.add_package_provider(provider);
        }
        ~RegistryFixture()
        {
            for (const auto& package : registry.get_all_packages())
                registry.unload(package);
            sys.attr("path") = saved_path;
        }
        void write_manifest(const std::string& name, const std::string& attributes = "")
        {
            directory.write(name + "/package.toml", fmt::format("[base]\nname = '{}'\n{}", name, attributes));
        }
    };

    std::shared_ptr<PackageSharedData> package_data(const std::string& name, const std::vector<std::string>& dependencies)
    {
        auto data = std::make_shared<PackageSharedData>();
        data->name = name;
        for (const auto& dependency : dependencies)
            data->direct_dependencies[dependency] = std::any(PackageAnyDictionary());
        return data;
    }
}

DOCTEST_TEST_SUITE("PackagingRegistry")
{
    DOCTEST_TEST_CASE_FIXTURE(RegistryFixture, "discovery_defaults_relative_root_and_duplicate_paths")
    {
        write_manifest("first");
        write_manifest("second", "root = '../shared'\n");
        directory.write("ignored/unrelated.toml", "[base]\nname = 'ignored'\n");
        provider->add_path((directory.path() / "*").generic_u8string().c_str());
        registry.fetch_packages();
        DOCTEST_CHECK(registry.get_all_packages().size() == 2);
        const auto first = registry.get_package("first");
        DOCTEST_REQUIRE(first.is_valid());
        DOCTEST_CHECK_FALSE(first.is_loaded());
        DOCTEST_CHECK(std::filesystem::equivalent(std::filesystem::u8path(first.get_root_dir()), directory.path() / "first"));
        DOCTEST_CHECK(packaging_tests::values_equal(first.get_attribute("base.first_entry_point"), std::any(std::string("cpp"))));
        DOCTEST_CHECK(packaging_tests::values_equal(first.get_attribute("base.unloadable"), std::any(false)));
        DOCTEST_CHECK(first.get_direct_dependencies().empty());
        DOCTEST_CHECK(packaging_tests::values_equal(first.get_attribute("native.entry_point"), std::any(PackageAnyArray())));
        DOCTEST_CHECK(packaging_tests::values_equal(first.get_attribute("python.entry_point"), std::any(PackageAnyArray())));
        DOCTEST_CHECK(!first.get_attribute("not_defined").has_value());
        DOCTEST_CHECK(registry.get_package("second").get_root_dir() == (directory.path() / "shared").generic_u8string());
        DOCTEST_CHECK_FALSE(registry.get_package("unknown").is_valid());
        DOCTEST_CHECK_FALSE(registry.load("unknown"));
        DOCTEST_CHECK_FALSE(registry.unload("unknown"));
    }

    DOCTEST_TEST_CASE_FIXTURE(RegistryFixture, "token_substitution_in_nested_tables_arrays_and_keys")
    {
        DOCTEST_REQUIRE(registry.define_token("TEST_ROOT", "resolved"));
        write_manifest("tokens", "root = '${TEST_ROOT}'\n[test]\ntext = 'pre-${TEST_ROOT}-${TEST_ROOT}-post'\n"
                                 "unknown = '${UNDEFINED_TEST_TOKEN}'\nvalues = ['${TEST_ROOT}', 'literal']\n"
                                 "[test.nested]\n'${TEST_ROOT}' = '${TEST_ROOT}'\n"
                                 "[[native.entry_point]]\npath = '${LIB_PREFIX}plugin${LIB_EXT}'\n");
        registry.fetch_packages();
        const auto package = registry.get_package("tokens");
        DOCTEST_REQUIRE(package.is_valid());
        DOCTEST_CHECK(packaging_tests::values_equal(package.get_attribute("test.text"), std::any(std::string("pre-resolved-resolved-post"))));
        DOCTEST_CHECK(packaging_tests::values_equal(package.get_attribute("test.nested.resolved"), std::any(std::string("resolved"))));
        DOCTEST_CHECK(packaging_tests::values_equal(package.get_attribute("test.unknown"), std::any(std::string("${UNDEFINED_TEST_TOKEN}"))));
        DOCTEST_CHECK(packaging_tests::values_equal(
            package.get_attribute("test.values"), std::any(PackageAnyArray { std::any(std::string("resolved")), std::any(std::string("literal")) })));
        DOCTEST_CHECK(package.get_root_dir() == (directory.path() / "tokens" / "resolved").generic_u8string());
        const auto entries = std::any_cast<PackageAnyArray>(package.get_attribute("native.entry_point"));
        DOCTEST_REQUIRE(entries.size() == 1);
        const auto library_path = find_package_attribute(std::any_cast<const PackageAnyDictionary&>(entries[0]), "path");
        DOCTEST_REQUIRE(library_path);
#ifdef OPENDCC_OS_WINDOWS
        DOCTEST_CHECK(packaging_tests::values_equal(library_path->get(), std::any(std::string("plugin.dll"))));
#elif defined(OPENDCC_OS_MAC)
        DOCTEST_CHECK(packaging_tests::values_equal(library_path->get(), std::any(std::string("libplugin.dylib"))));
#else
        DOCTEST_CHECK(packaging_tests::values_equal(library_path->get(), std::any(std::string("libplugin.so"))));
#endif
        DOCTEST_CHECK(registry.undefine_token("TEST_ROOT"));
        DOCTEST_CHECK_FALSE(registry.undefine_token("TEST_ROOT"));
    }

    DOCTEST_TEST_CASE_FIXTURE(RegistryFixture, "provider_removal_and_refetch_update_provider_cache")
    {
        write_manifest("one");
        provider->fetch();
        DOCTEST_CHECK(provider->get_cached_packages().size() == 1);
        const auto pattern = (directory.path() / "*").generic_u8string();
        provider->remove_path(pattern.c_str());
        provider->fetch();
        DOCTEST_CHECK(provider->get_cached_packages().empty());
        registry.remove_package_provider(provider);
        registry.fetch_packages();
        DOCTEST_CHECK(registry.get_all_packages().empty());
    }

    DOCTEST_TEST_CASE_FIXTURE(RegistryFixture, "autoload_and_explicit_load_fetched")
    {
        write_manifest("automatic", "autoload = true\n");
        write_manifest("manual");
        registry.fetch_packages();
        DOCTEST_CHECK(registry.get_package("automatic").is_loaded());
        DOCTEST_CHECK_FALSE(registry.get_package("manual").is_loaded());
        DOCTEST_CHECK(registry.load("manual"));
        DOCTEST_CHECK(registry.load("manual"));
        DOCTEST_CHECK(registry.unload("manual"));
        DOCTEST_CHECK(registry.unload("manual"));
        registry.fetch_packages(true);
        DOCTEST_CHECK(registry.get_package("automatic").is_loaded());
        DOCTEST_CHECK(registry.get_package("manual").is_loaded());
    }

    DOCTEST_TEST_CASE("dependency_chains_and_diamonds")
    {
        const std::unordered_map<std::string, std::shared_ptr<PackageSharedData>> packages = {
            { "root", package_data("root", { "left", "right" }) },
            { "left", package_data("left", { "leaf" }) },
            { "right", package_data("right", { "leaf" }) },
            { "leaf", package_data("leaf", {}) },
        };
        PackageResolver resolver;
        resolver.set_packages(packages);
        const auto dependencies = resolver.get_dependencies("root");
        DOCTEST_REQUIRE(dependencies.size() == packages.size());
        const auto position = [&dependencies](const std::string& name) {
            return std::find(dependencies.begin(), dependencies.end(), name);
        };
        DOCTEST_CHECK(position("leaf") < position("left"));
        DOCTEST_CHECK(position("leaf") < position("right"));
        DOCTEST_CHECK(position("left") < position("root"));
        DOCTEST_CHECK(position("right") < position("root"));
        const auto dependees = resolver.get_dependees("leaf");
        DOCTEST_REQUIRE(dependees.size() == packages.size());
        DOCTEST_CHECK(dependees.front() == "root");
        DOCTEST_CHECK(dependees.back() == "leaf");
        DOCTEST_CHECK(resolver.get_dependencies("unknown").empty());
        resolver.set_packages({ { "single", package_data("single", {}) } });
        DOCTEST_CHECK(resolver.get_dependencies("root").empty());
        DOCTEST_CHECK(resolver.get_dependencies("single") == std::vector<std::string> { "single" });
    }

    DOCTEST_TEST_CASE_FIXTURE(RegistryFixture, "missing_dependency_and_cycles_fail_without_loading")
    {
        write_manifest("missing", "[dependencies]\nabsent = {}\n");
        write_manifest("cycle_a", "[dependencies]\ncycle_b = {}\n");
        write_manifest("cycle_b", "[dependencies]\ncycle_a = {}\n");
        write_manifest("self_cycle", "[dependencies]\nself_cycle = {}\n");
        write_manifest("independent");
        registry.fetch_packages();
        for (const auto& name : { "missing", "cycle_a", "cycle_b", "self_cycle" })
        {
            DOCTEST_CHECK_FALSE(registry.load(name));
            DOCTEST_CHECK_FALSE(registry.get_package(name).is_loaded());
        }
        DOCTEST_CHECK(registry.load("independent"));
    }
}

namespace
{
    class TestPackageProvider : public PackageProvider
    {
    public:
        explicit TestPackageProvider(const PackageData& data)
            : m_packages { data }
        {
        }

        void fetch() override {}
        const std::vector<PackageData>& get_cached_packages() const override { return m_packages; }

    private:
        std::vector<PackageData> m_packages;
    };
}

DOCTEST_TEST_SUITE("PackagingPackageRegistry")
{
    DOCTEST_TEST_CASE("UTF-8 package root")
    {
        PackageData data;
        data.name = "test_utf8_root";
#ifdef OPENDCC_OS_WINDOWS
        const auto prefix = std::string("C:");
#else
        const auto prefix = std::string();
#endif
        data.path = prefix + u8"/packages/\u0442\u0435\u0441\u0442";
        auto root = std::string(u8"assets/../\u8d44\u6e90");
        auto expected = prefix + u8"/packages/\u0442\u0435\u0441\u0442/\u8d44\u6e90";

        DOCTEST_SUBCASE("relative root") {}
        DOCTEST_SUBCASE("absolute root")
        {
            root = prefix + u8"/assets/\u8d44\u6e90";
            expected = root;
        }

        data.raw_attributes.push_back({ "base", std::any(PackageAnyDictionary { { "root", std::any(root) } }) });
        PackageRegistry registry;
        registry.add_package_provider(std::make_shared<TestPackageProvider>(data));
        registry.fetch_packages();

        DOCTEST_CHECK_EQ(registry.get_package(data.name).get_root_dir(), expected);
    }
}

OPENDCC_NAMESPACE_CLOSE
