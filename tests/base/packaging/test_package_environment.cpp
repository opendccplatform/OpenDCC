/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "package_lifecycle_fixture.h"
#include "opendcc/base/fs_utils/defines.h"
#include <algorithm>
#include <map>
#include <optional>
#ifdef OPENDCC_OS_WINDOWS
#include <Windows.h>
#else
#include <cstdlib>
#endif

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

namespace
{
    namespace py = pybind11;

    std::optional<std::string> environment_value(const std::string& name)
    {
#ifdef OPENDCC_OS_WINDOWS
        SetLastError(ERROR_SUCCESS);
        if (GetEnvironmentVariableA(name.c_str(), nullptr, 0) == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
            return std::nullopt;
#else
        if (std::getenv(name.c_str()) == nullptr)
            return std::nullopt;
#endif
        return base::utils::get_env(name);
    }

    void restore_environment(const std::string& name, const std::optional<std::string>& value)
    {
        if (value)
            base::utils::set_env(name, *value);
        else
        {
#ifdef OPENDCC_OS_WINDOWS
            SetEnvironmentVariableA(name.c_str(), nullptr);
#else
            unsetenv(name.c_str());
#endif
        }
    }

    struct EnvironmentFixture : packaging_tests::LifecycleFixture
    {
        const std::string module_name = "packaging_environment_probe";
        const std::string setting_name = "OPENDCC_TEST_MODE";
        const std::string asset_name = "OPENDCC_TEST_ASSET";
        std::map<std::string, std::optional<std::string>> saved_values;
        py::object saved_probe = modules.contains(module_name.c_str()) ? modules[module_name.c_str()].cast<py::object>() : py::none();

        EnvironmentFixture()
        {
            for (const auto& name : { std::string("PATH"), std::string("PYTHONPATH"), setting_name, asset_name })
                saved_values[name] = environment_value(name);
            if (modules.contains(module_name.c_str()))
                modules.attr("pop")(module_name);
        }
        ~EnvironmentFixture()
        {
            for (const auto& item : saved_values)
                restore_environment(item.first, item.second);
            if (modules.contains(module_name.c_str()))
                modules.attr("pop")(module_name);
            if (!saved_probe.is_none())
                modules[module_name.c_str()] = saved_probe;
        }

        void write_environment_manifest(const std::string& name, const std::string& entries, const std::string& root = ".",
                                        const std::string& first_entry_point = "cpp")
        {
            directory.write(name + "/package.toml",
                            fmt::format(R"toml([base]
name = '{}'
root = '{}'
first_entry_point = '{}'

[test]
event_log = '{}'
environment_variable = '{}'
fail_initialize = false

{}
)toml",
                                        name, root, first_entry_point, (directory.path() / "events.txt").generic_u8string(), setting_name, entries));
        }

        bool has_python_path(const std::string& path) const { return sys.attr("path").attr("__contains__")(path).cast<bool>(); }
        std::string setting() const { return base::utils::get_env(setting_name); }
    };
}

DOCTEST_TEST_SUITE("PackagingEnvironment")
{
    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "path_entries_prepend_resolved_paths_without_reapplying_a_loaded_package")
    {
        DOCTEST_REQUIRE(registry.define_token("TEST_BIN", "bin"));
        const auto external_path = (directory.path() / "external").generic_u8string();
        write_manifest("paths", fmt::format(R"toml(
[[environment.PATH]]
value = '${{TEST_BIN}}'

[[environment.PATH]]
value = '{}'
)toml",
                                            external_path));
        registry.fetch_packages();
        const auto package = registry.get_package("paths");
        DOCTEST_REQUIRE(registry.load(package));
        const auto relative_path = (std::filesystem::u8path(package.get_root_dir()) / "bin").lexically_normal().u8string();
        const auto absolute_path = std::filesystem::u8path(external_path).lexically_normal().u8string();
        const auto expected = absolute_path + OPENDCC_PATH_LIST_SEPARATOR + relative_path + OPENDCC_PATH_LIST_SEPARATOR + saved_environment_path;
        DOCTEST_CHECK(base::utils::get_env("PATH") == expected);
        DOCTEST_REQUIRE(registry.load(package));
        DOCTEST_CHECK(base::utils::get_env("PATH") == expected);
        DOCTEST_REQUIRE(registry.unload(package));
        DOCTEST_CHECK(base::utils::get_env("PATH") == expected);
    }

    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "pythonpath_environment_is_inherited_without_changing_runtime_search_paths")
    {
        directory.write("python_paths/python/packaging_environment_probe.py", "marker = 'from-package'\n");
        write_manifest("python_paths", R"toml(
[[environment.PYTHONPATH]]
value = 'python'
is_path = true
operation = 'append'
)toml");
        registry.fetch_packages();
        const auto package = registry.get_package("python_paths");
        DOCTEST_REQUIRE(registry.load(package));
        const auto python_path = (std::filesystem::u8path(package.get_root_dir()) / "python").generic_u8string();
        DOCTEST_CHECK_FALSE(has_python_path(python_path));
        DOCTEST_CHECK_FALSE(modules.contains(module_name.c_str()));
        const auto command = py::make_tuple(OPENDCC_PACKAGING_TEST_PYTHON, "-c", "import packaging_environment_probe as probe; print(probe.marker)");
        const auto output = py::module_::import("subprocess").attr("check_output")(command, py::arg("text") = true).cast<std::string>();
        DOCTEST_CHECK(output == "from-package\n");
        const auto process_python_path = base::utils::get_env("PYTHONPATH");
        DOCTEST_CHECK(process_python_path.find((std::filesystem::u8path(package.get_root_dir()) / "python").lexically_normal().u8string()) !=
                      std::string::npos);
        const auto path_after_load = py::list(sys.attr("path"));
        DOCTEST_REQUIRE(registry.load(package));
        DOCTEST_CHECK(sys.attr("path").cast<py::list>().equal(path_after_load));
        DOCTEST_CHECK(base::utils::get_env("PYTHONPATH") == process_python_path);
        DOCTEST_REQUIRE(registry.unload(package));
        DOCTEST_CHECK(sys.attr("path").cast<py::list>().equal(path_after_load));
    }

    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "pythonpath_environment_defaults_to_a_literal_process_value")
    {
        write_environment_manifest("literal_pythonpath", R"toml(
[[environment.PYTHONPATH]]
value = 'relative-search-path'
)toml");
        registry.fetch_packages();
        const auto package = registry.get_package("literal_pythonpath");
        DOCTEST_REQUIRE(registry.load(package));
        DOCTEST_CHECK(base::utils::get_env("PYTHONPATH") == "relative-search-path");
        DOCTEST_CHECK_FALSE(has_python_path("relative-search-path"));
        DOCTEST_CHECK_FALSE(has_python_path((std::filesystem::u8path(package.get_root_dir()) / "relative-search-path").generic_u8string()));
    }

    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "ordinary_variables_are_available_to_native_entry_points_and_child_processes")
    {
        std::string first = "cpp";
        DOCTEST_SUBCASE("native first")
        {
            first = "cpp";
        }
        DOCTEST_SUBCASE("Python first")
        {
            first = "python";
        }
        DOCTEST_REQUIRE(registry.define_token("TEST_MODE", "enabled"));
        directory.write("mixed/python/packaging_environment_probe.py",
                        fmt::format(R"python(import subprocess
value = subprocess.check_output(
    [{}, "-c", "import os; print(os.environ['OPENDCC_TEST_MODE'])"], text=True
).strip()
)python",
                                    py::repr(py::str(OPENDCC_PACKAGING_TEST_PYTHON)).cast<std::string>()));
        write_environment_manifest("mixed", R"toml(
[[environment.OPENDCC_TEST_MODE]]
value = 'replaced'

[[environment.OPENDCC_TEST_MODE]]
value = '${TEST_MODE}'

[[python.path]]
path = 'python'

[[python.import]]
module = 'packaging_environment_probe'
)toml" + native_entry_point(),
                                   ".", first);
        registry.fetch_packages();
        const auto package = registry.get_package("mixed");
        DOCTEST_REQUIRE(registry.load(package));
        DOCTEST_CHECK(base::utils::get_env(setting_name) == "enabled");
        DOCTEST_CHECK(setting() == "enabled");
        DOCTEST_CHECK(modules[module_name.c_str()].attr("value").cast<std::string>() == "enabled");
        const auto observed = events();
        DOCTEST_CHECK(std::find(observed.begin(), observed.end(), "native:environment:enabled") != observed.end());
        DOCTEST_REQUIRE(registry.unload(package));
        DOCTEST_CHECK(setting() == "enabled");
    }

    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "explicit_operations_apply_in_manifest_order_with_custom_separators")
    {
        DOCTEST_REQUIRE(base::utils::set_env(setting_name, "original"));
        write_environment_manifest("operations", R"toml(
[[environment.OPENDCC_TEST_MODE]]
value = 'middle'
operation = 'set'

[[environment.OPENDCC_TEST_MODE]]
value = 'last'
operation = 'append'
separator = '|'

[[environment.OPENDCC_TEST_MODE]]
value = 'first'
operation = 'prepend'
separator = '|'
)toml");
        registry.fetch_packages();
        DOCTEST_REQUIRE(registry.load("operations"));
        DOCTEST_CHECK(setting() == "first|middle|last");
    }

    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "package_root_paths_and_literal_tokens_are_independent_options")
    {
        DOCTEST_REQUIRE(registry.define_token("TEST_MODE", "expanded"));
        write_environment_manifest("roots", R"toml(
[[environment.OPENDCC_TEST_ASSET]]
value = '${PACKAGE_ROOT}/assets'
is_path = true

[[environment.OPENDCC_TEST_MODE]]
value = '${TEST_MODE}/${PACKAGE_ROOT}'
expand_tokens = false
)toml",
                                   "content");
        write_environment_manifest("relative_root", R"toml(
[[environment.OPENDCC_TEST_ASSET]]
value = 'assets'
is_path = true
)toml",
                                   "content");
        registry.fetch_packages();
        const auto package = registry.get_package("roots");
        DOCTEST_REQUIRE(registry.load(package));
        const auto expected = (std::filesystem::u8path(package.get_root_dir()) / "assets").lexically_normal().u8string();
        DOCTEST_CHECK(base::utils::get_env(asset_name) == expected);
        DOCTEST_CHECK(setting() == "${TEST_MODE}/${PACKAGE_ROOT}");
        const auto relative_package = registry.get_package("relative_root");
        DOCTEST_REQUIRE(registry.load(relative_package));
        const auto relative_expected = (std::filesystem::u8path(relative_package.get_root_dir()) / "assets").lexically_normal().u8string();
        DOCTEST_CHECK(base::utils::get_env(asset_name) == relative_expected);
    }

    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "python_path_set_preserves_initial_paths_and_prepend_controls_search_priority")
    {
        const auto process_python_path = base::utils::get_env("PYTHONPATH");
        const auto initial_paths = sys.attr("path").cast<py::list>().cast<std::vector<std::string>>();
        const auto startup_path = std::find_if(initial_paths.begin(), initial_paths.end(), [](const std::string& path) { return !path.empty(); });
        DOCTEST_REQUIRE(startup_path != initial_paths.end());
        DOCTEST_REQUIRE(registry.define_token("TEST_STARTUP_PATH", *startup_path));
        write_environment_manifest("search", R"toml(
[[python.path]]
path = 'discarded'
operation = 'append'

[[python.path]]
path = '${TEST_STARTUP_PATH}'
is_path = false
operation = 'append'

[[python.path]]
path = 'kept'
operation = 'set'

[[python.path]]
path = 'priority'
operation = 'prepend'
)toml");
        registry.fetch_packages();
        const auto package = registry.get_package("search");
        DOCTEST_REQUIRE(registry.load(package));
        const auto root = std::filesystem::u8path(package.get_root_dir());
        const auto priority = (root / "priority").lexically_normal().generic_u8string();
        DOCTEST_CHECK_FALSE(has_python_path((root / "discarded").lexically_normal().generic_u8string()));
        DOCTEST_CHECK(has_python_path((root / "kept").lexically_normal().generic_u8string()));
        DOCTEST_CHECK(sys.attr("path").cast<py::list>()[0].cast<std::string>() == priority);
        for (const auto& path : initial_paths)
            DOCTEST_CHECK(has_python_path(path));
        const auto actual_paths = sys.attr("path").cast<py::list>().cast<std::vector<std::string>>();
        DOCTEST_REQUIRE(actual_paths.size() >= initial_paths.size() + 1);
        DOCTEST_CHECK(std::equal(initial_paths.begin(), initial_paths.end(), actual_paths.begin() + 1));
        DOCTEST_CHECK(std::count(actual_paths.begin(), actual_paths.end(), *startup_path) ==
                      std::count(initial_paths.begin(), initial_paths.end(), *startup_path));
        DOCTEST_CHECK(base::utils::get_env("PYTHONPATH") == process_python_path);
    }

    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "empty_values_and_invalid_entries_do_not_apply_unintended_changes")
    {
        DOCTEST_REQUIRE(base::utils::set_env(setting_name, "before"));
        write_environment_manifest("invalid", R"toml(
[[environment.OPENDCC_TEST_MODE]]
operation = 'set'

[[environment.OPENDCC_TEST_MODE]]
value = 42

[[environment.OPENDCC_TEST_MODE]]
value = 'ignored'
operation = 'unknown'

[[environment.OPENDCC_TEST_MODE]]
value = 'ignored'
is_path = 'not a boolean'
)toml");
        write_environment_manifest("empty", R"toml(
[[environment.OPENDCC_TEST_MODE]]
value = ''
)toml");
        registry.fetch_packages();
        DOCTEST_REQUIRE(registry.load("invalid"));
        DOCTEST_CHECK(setting() == "before");
        DOCTEST_CHECK(base::utils::get_env(setting_name) == "before");
        DOCTEST_REQUIRE(registry.load("empty"));
        DOCTEST_CHECK(setting().empty());
        DOCTEST_CHECK(base::utils::get_env(setting_name).empty());
        DOCTEST_CHECK(environment_value(setting_name).has_value());
    }

    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "python_paths_change_runtime_search_without_changing_the_environment")
    {
        const auto process_python_path = base::utils::get_env("PYTHONPATH");
        DOCTEST_REQUIRE(registry.define_token("TEST_PY_DIR", "python"));
        directory.write("runtime/python/packaging_environment_probe.py", "marker = 'runtime-only'\n");
        write_environment_manifest("runtime", R"toml(
[[python.path]]
path = 'discarded'

[[python.path]]
path = '${TEST_PY_DIR}'
operation = 'set'

[[python.path]]
path = 'priority'
operation = 'prepend'

[[python.import]]
module = 'packaging_environment_probe'
)toml");
        registry.fetch_packages();
        const auto package = registry.get_package("runtime");
        DOCTEST_REQUIRE(registry.load(package));
        const auto root = std::filesystem::u8path(package.get_root_dir());
        DOCTEST_CHECK_FALSE(has_python_path((root / "discarded").lexically_normal().generic_u8string()));
        DOCTEST_CHECK(has_python_path((root / "python").lexically_normal().generic_u8string()));
        DOCTEST_CHECK(sys.attr("path").cast<py::list>()[0].cast<std::string>() == (root / "priority").lexically_normal().generic_u8string());
        DOCTEST_CHECK(modules[module_name.c_str()].attr("marker").cast<std::string>() == "runtime-only");
        DOCTEST_CHECK(base::utils::get_env("PYTHONPATH") == process_python_path);
        const auto path_after_load = py::list(sys.attr("path"));
        DOCTEST_REQUIRE(registry.load(package));
        DOCTEST_CHECK(sys.attr("path").cast<py::list>().equal(path_after_load));
        const auto command = py::make_tuple(OPENDCC_PACKAGING_TEST_PYTHON, "-c",
                                            "import importlib.util; print(importlib.util.find_spec('packaging_environment_probe'))");
        const auto output = py::module_::import("subprocess").attr("check_output")(command, py::arg("text") = true).cast<std::string>();
        DOCTEST_CHECK(output == "None\n");
    }

    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "child_process_inherits_environment_values_and_pythonpath")
    {
        directory.write("child/python/packaging_environment_probe.py", "import os\nvalue = os.environ['OPENDCC_TEST_MODE']\n");
        write_environment_manifest("child", R"toml(
[[environment.OPENDCC_TEST_MODE]]
value = 'inherited'

[[environment.PYTHONPATH]]
value = 'python'
is_path = true
)toml");
        registry.fetch_packages();
        DOCTEST_REQUIRE(registry.load("child"));
        const auto command = py::make_tuple(OPENDCC_PACKAGING_TEST_PYTHON, "-c", "import packaging_environment_probe as probe; print(probe.value)");
        const auto output = py::module_::import("subprocess").attr("check_output")(command, py::arg("text") = true).cast<std::string>();
        DOCTEST_CHECK(output == "inherited\n");
    }

    DOCTEST_TEST_CASE_FIXTURE(EnvironmentFixture, "path_defaults_can_be_overridden_for_literal_values")
    {
        DOCTEST_REQUIRE(registry.define_token("TEST_BIN", "expanded-bin"));
        write_environment_manifest("literal_path", R"toml(
[[environment.PATH]]
value = '${TEST_BIN}'
operation = 'set'
is_path = false
expand_tokens = false
)toml");
        registry.fetch_packages();
        DOCTEST_REQUIRE(registry.load("literal_path"));
        DOCTEST_CHECK(base::utils::get_env("PATH") == "${TEST_BIN}");
    }
}

OPENDCC_NAMESPACE_CLOSE
