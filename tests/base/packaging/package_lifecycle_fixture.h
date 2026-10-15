/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/base/pybind_bridge/pybind11.h"
#include "opendcc/base/packaging/package_registry.h"
#include "opendcc/base/packaging/filesystem_package_provider.h"
#include "opendcc/base/utils/env.h"
#include "opendcc/base/utils/library.h"
#include "opendcc/base/vendor/spdlog/fmt/fmt.h"
#include "package_test_utils.h"

OPENDCC_NAMESPACE_OPEN

namespace packaging_tests
{
    inline bool is_library_loaded(const std::string& path)
    {
        auto handle = base::utils::get_dl_handle(path);
#ifndef OPENDCC_OS_WINDOWS
        // RTLD_NOLOAD acquires a reference; release the probe's reference.
        if (handle)
            base::utils::dl_close(handle);
#endif
        return handle != nullptr;
    }

    struct LifecycleFixture
    {
        // Python members must be destroyed before the GIL is released.
        pybind11::gil_scoped_acquire gil;
        TemporaryDirectory directory;
        pybind11::module_ sys = pybind11::module_::import("sys");
        pybind11::list saved_path = pybind11::list(sys.attr("path"));
        pybind11::dict modules = sys.attr("modules");
        pybind11::object saved_module =
            modules.contains("lifecycle_entry_point") ? modules["lifecycle_entry_point"].cast<pybind11::object>() : pybind11::none();
        std::string saved_environment_path = base::utils::get_env("PATH");
        base::packaging::PackageRegistry registry;
        std::shared_ptr<base::packaging::FileSystemPackageProvider> provider = std::make_shared<base::packaging::FileSystemPackageProvider>();

        LifecycleFixture()
        {
            const auto source = std::filesystem::u8path(OPENDCC_PACKAGING_TEST_FIXTURES);
            std::filesystem::copy_file(source / "lifecycle_entry_point.py", directory.path() / "lifecycle_entry_point.py");
            if (modules.contains("lifecycle_entry_point"))
                modules.attr("pop")("lifecycle_entry_point");
            provider->add_path((directory.path() / "*").generic_u8string().c_str());
            registry.add_package_provider(provider);
        }
        ~LifecycleFixture()
        {
            for (const auto& package : registry.get_all_packages())
                registry.unload(package);
            sys.attr("path") = saved_path;
            if (modules.contains("lifecycle_entry_point"))
                modules.attr("pop")("lifecycle_entry_point");
            if (!saved_module.is_none())
                modules["lifecycle_entry_point"] = saved_module;
            base::utils::set_env("PATH", saved_environment_path);
        }
        void write_manifest(const std::string& name, const std::string& entry_points, const std::string& first_entry_point = "cpp",
                            const std::string& dependencies = "", bool fail_initialize = false)
        {
            const auto event_log = (directory.path() / "events.txt").generic_u8string();
            const auto manifest = fmt::format(R"toml([base]
name = '{}'
first_entry_point = '{}'

[test]
event_log = '{}'
fail_initialize = {}

[[python.path]]
path = '..'
{}{})toml",
                                              name, first_entry_point, event_log, fail_initialize, entry_points, dependencies);
            directory.write(name + "/package.toml", manifest);
        }
        std::string native_entry_point() const { return fmt::format("[[native.entry_point]]\npath = '{}'\n", OPENDCC_PACKAGING_LIFECYCLE_LIBRARY); }
        std::string python_entry_point() const { return "[[python.entry_point]]\nmodule = 'lifecycle_entry_point'\n"; }
        std::vector<std::string> events() const { return read_events(directory.path() / "events.txt"); }
        std::vector<std::string> init_and_uninit_events() const
        {
            // Destruction is checked separately by the lifetime tests.
            std::vector<std::string> result;
            for (const auto& event : events())
                if (event.find(":destroy:") == std::string::npos)
                    result.push_back(event);
            return result;
        }
        std::string events_summary() const
        {
            std::string result;
            for (const auto& event : events())
                result += event + "\n";
            return result;
        }
    };
}

OPENDCC_NAMESPACE_CLOSE
