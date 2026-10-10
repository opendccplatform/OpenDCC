// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/pybind_bridge/pybind11.h"
#include "opendcc/base/packaging/package_loader.h"
#include "opendcc/base/packaging/package_resolver.h"
#include "opendcc/base/vendor/ghc/filesystem.hpp"
#include "opendcc/base/utils/library.h"
#include <pxr/base/vt/array.h>
#include <cstdio>
#ifdef OPENDCC_OS_WINDOWS
#include <Windows.h>
#endif

#ifdef OPENDCC_OS_WINDOWS
#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>
OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

using namespace pybind11;

DOCTEST_TEST_SUITE("PackagingPackageLoader")
{
    DOCTEST_TEST_CASE("load")
    {
        char tmp_dir_str[1024] = {};
        tmpnam(tmp_dir_str);
        DOCTEST_CHECK(ghc::filesystem::create_directory(tmp_dir_str));
        auto tests_a_dll_hndl = dl_open("packaging_tests_a.dll", DONT_RESOLVE_DLL_REFERENCES);
        auto tests_b_dll_hndl = dl_open("packaging_tests_b.dll", DONT_RESOLVE_DLL_REFERENCES);
        char dll_path_a[512];
        char dll_path_b[512];
        GetModuleFileName(static_cast<HMODULE>(tests_a_dll_hndl), dll_path_a, sizeof(dll_path_a));
        GetModuleFileName(static_cast<HMODULE>(tests_b_dll_hndl), dll_path_b, sizeof(dll_path_b));
        dl_close(tests_a_dll_hndl);
        dl_close(tests_b_dll_hndl);

        auto tmp_dir = ghc::filesystem::path(tmp_dir_str);
        auto tmp_path = tmp_dir / "packaging_tests_a.dll";
        auto test_lib_path = ghc::filesystem::path(dll_path_a);
        ghc::filesystem::copy_file(test_lib_path, tmp_path, ghc::filesystem::copy_options::overwrite_existing);
        tmp_path = tmp_dir / "packaging_tests_b.dll";
        test_lib_path = ghc::filesystem::path(dll_path_b);
        ghc::filesystem::copy_file(test_lib_path, tmp_path, ghc::filesystem::copy_options::overwrite_existing);

        ghc::filesystem::copy(ghc::filesystem::path(OPENDCC_PACKAGING_TEST_FIXTURES) / "packaging_tests", tmp_dir / "packaging_tests",
                              ghc::filesystem::copy_options::recursive);

        std::unordered_map<std::string, std::shared_ptr<PackageSharedData>> pkg_shared_data;
        auto test_package_data = std::make_shared<PackageSharedData>();
        test_package_data->name = "test_name";
        test_package_data->loaded = false;
        test_package_data->root_dir = tmp_dir.lexically_normal().generic_string();
        VtDictionary dict;
        dict.SetValueAtPath("base.name", VtValue("test_name"), ".");
        dict.SetValueAtPath("base.unloadable", VtValue(true), ".");
        dict.SetValueAtPath("native.entry_point",
                            VtValue(VtArray<VtDictionary>({
                                VtDictionary({ { "path", VtValue("packaging_tests_a.dll") } }),
                                VtDictionary({ { "path", VtValue("packaging_tests_b.dll") } }),
                            })),
                            ".");
        dict.SetValueAtPath("python.entry_point",
                            VtValue(VtArray<VtDictionary>({
                                VtDictionary({ { "module", VtValue("packaging_tests.a") } }),
                                VtDictionary({ { "module", VtValue("packaging_tests.b") } }),
                            })),
                            ".");

        test_package_data->resolved_attributes = dict;
        test_package_data->direct_dependencies = VtDictionary();
        pkg_shared_data[test_package_data->name] = test_package_data;
        auto package_resolver = std::make_shared<PackageResolver>();
        auto package_loader = std::make_shared<PackageLoader>(package_resolver, pkg_shared_data);
        package_resolver->set_packages(pkg_shared_data);
        DOCTEST_SUBCASE("multiple_cpp_entry_points")
        {
            DOCTEST_CHECK(package_loader->load("test_name"));

            auto hndl1 = GetModuleHandle("packaging_tests_a");
            auto hndl2 = GetModuleHandle("packaging_tests_b");
            int* entry_point_checker1 = reinterpret_cast<int*>(dl_sym(hndl1, "s_entry_point_checker"));
            int* entry_point_checker2 = reinterpret_cast<int*>(dl_sym(hndl2, "s_entry_point_checker"));
            // Check C++ entry points
            DOCTEST_CHECK(*entry_point_checker1 == 1); // Initialized after load
            DOCTEST_CHECK(*entry_point_checker2 == 1);

            // Check Python entry points
            {
                gil_scoped_acquire l;

                DOCTEST_CHECK_EQ(module_::import("packaging_tests.a").attr("entry_point_checker").cast<uint32_t>(), 1);
                DOCTEST_CHECK_EQ(module_::import("packaging_tests.b").attr("entry_point_checker").cast<uint32_t>(), 1);
            }
            DOCTEST_CHECK(package_loader->unload("test_name"));

            DOCTEST_CHECK(!GetModuleHandle("packaging_tests_a"));
            DOCTEST_CHECK(!GetModuleHandle("packaging_tests_b"));
        }

        std::remove(tmp_dir_str);
    }
}

OPENDCC_NAMESPACE_CLOSE
#endif
