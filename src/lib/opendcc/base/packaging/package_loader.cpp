// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/package_loader.h"
#include <filesystem>
#include <algorithm>
#include "opendcc/base/logging/logger.h"
#include "opendcc/base/pybind_bridge/pybind11.h"
#if defined(OPENDCC_OS_WINDOWS)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include "opendcc/base/utils/library.h"
#include "package_entry_point.h"
#include "opendcc/base/utils/string_utils.h"
#include "opendcc/base/utils/env.h"
#include "opendcc/base/fs_utils/defines.h"

using namespace pybind11;
OPENDCC_NAMESPACE_OPEN

namespace
{
    std::filesystem::path make_absolute_path(const std::string& root, const std::string& path)
    {
        auto result = std::filesystem::u8path(path);
        if (result.is_relative())
        {
            result = std::filesystem::u8path(root) / result;
        }
        return result;
    }

    class PyLock
    {
    public:
        PyLock() { m_state = PyGILState_Ensure(); }
        ~PyLock() { PyGILState_Release(m_state); }

    private:
        PyGILState_STATE m_state;
    };

    struct NativeLibrary : PackageSharedData::LoadedEntity
    {
        void* handle = nullptr;

        virtual bool close() override { return !dl_close(handle); }
        virtual void uninitialize(const Package& package) {}
    };
    struct NativeEntryPoint : NativeLibrary
    {
        PackageEntryPoint* entry_point = nullptr;

        virtual void uninitialize(const Package& package) override { entry_point->uninitialize(package); }
    };

    struct PythonEntryPoint : PackageSharedData::LoadedEntity
    {
        object entry_point_obj;
        bool close() override { return true; }

        virtual void uninitialize(const Package& package) override { as_entry_point()->uninitialize(package); }
        PackageEntryPoint* as_entry_point() { return entry_point_obj.cast<PackageEntryPoint*>(); }
    };
};

PackageLoader::PackageLoader(std::shared_ptr<PackageResolver> pkg_resolver, PackageSharedDataMap& pkg_shared_data)
    : m_pkg_resolver(pkg_resolver)
    , m_pkg_shared_data(pkg_shared_data)
{
}

PackageLoader::~PackageLoader() {}

bool PackageLoader::load(const std::string& pkg_name)
{
    auto data_it = m_pkg_shared_data.find(pkg_name);
    if (data_it == m_pkg_shared_data.end())
    {
        OPENDCC_ERROR("Failed to load package '{}': package is unknown.", pkg_name);
        return false;
    }

    auto& data = data_it->second;
    if (data->loaded)
    {
        return true;
    }

    auto deps = m_pkg_resolver->get_dependencies(data->name);

    if (deps.empty())
    {
        OPENDCC_ERROR("Failed to load package '{}'.", pkg_name);
        return false;
    }

    for (const auto& dep : deps)
    {
        auto it = m_pkg_shared_data.find(dep);
        if (it == m_pkg_shared_data.end())
        {
            OPENDCC_ERROR("Failed to load package '{}': package data for '{}' is not found.", pkg_name, dep);
            return false;
        }

        const auto& pkg_data = it->second;
        if (pkg_data->loaded)
        {
            continue;
        }

        OPENDCC_INFO("Loading package '{}'", pkg_data->name);
        // extend python environment modules because C++ code might depend on it, in theory
        if (!extend_python_path(pkg_data->root_dir, pkg_name, pkg_data->name))
        {
            return false;
        }

        apply_environment(pkg_data);
        apply_python_paths(pkg_data, pkg_name);

        const auto first_entry_point = pkg_data->get_resolved("base.first_entry_point", "cpp");
        if (first_entry_point == "cpp")
        {
            load_cpp_libs(pkg_data);
            load_python_modules(pkg_data);
        }
        else if (first_entry_point == "python")
        {
            load_python_modules(pkg_data);
            load_cpp_libs(pkg_data);
        }
        pkg_data->loaded = true;
    }

    return true;
}

void PackageLoader::apply_environment(const std::shared_ptr<PackageSharedData>& pkg_data)
{
    const auto environment = find_package_attribute(pkg_data->resolved_attributes, "environment");
    if (!environment)
        return;
    if (!std::any_cast<PackageAnyDictionary>(&environment->get()))
    {
        OPENDCC_WARN("Invalid environment table for package '{}'.", pkg_data->name);
        return;
    }

    for (const auto& variable : std::any_cast<const PackageAnyDictionary&>(environment->get()))
    {
        const auto& name = variable.first;
        if (!std::any_cast<PackageAnyArray>(&variable.second))
        {
            OPENDCC_WARN("Invalid environment entries for '{}' in package '{}'.", name, pkg_data->name);
            continue;
        }
        const bool path_variable = name == "PATH";
        for (const auto& entry_value : std::any_cast<const PackageAnyArray&>(variable.second))
        {
            const auto entry = std::any_cast<PackageAnyDictionary>(&entry_value);
            if (!entry)
            {
                OPENDCC_WARN("Invalid environment entry for '{}' in package '{}'.", name, pkg_data->name);
                continue;
            }
            const auto value = find_package_attribute(*entry, "value");
            const auto operation_attr = find_package_attribute(*entry, "operation");
            const auto path_attr = find_package_attribute(*entry, "is_path");
            const auto expand_attr = find_package_attribute(*entry, "expand_tokens");
            const auto separator_attr = find_package_attribute(*entry, "separator");
            if (!value || !std::any_cast<std::string>(&value->get()) || (operation_attr && !std::any_cast<std::string>(&operation_attr->get())) ||
                (path_attr && !std::any_cast<bool>(&path_attr->get())) || (expand_attr && !std::any_cast<bool>(&expand_attr->get())) ||
                (separator_attr && !std::any_cast<std::string>(&separator_attr->get())))
            {
                OPENDCC_WARN("Invalid environment entry for '{}' in package '{}'.", name, pkg_data->name);
                continue;
            }
            const auto operation = operation_attr ? std::any_cast<const std::string&>(operation_attr->get()) : path_variable ? "prepend" : "set";
            if (operation != "set" && operation != "append" && operation != "prepend")
            {
                OPENDCC_WARN("Unknown environment operation '{}' for '{}' in package '{}'.", operation, name, pkg_data->name);
                continue;
            }
            const bool is_path = path_attr ? std::any_cast<const bool&>(path_attr->get()) : path_variable;
            auto resolved_value = std::any_cast<const std::string&>(value->get());
            if (is_path && !resolved_value.empty())
                resolved_value = make_absolute_path(pkg_data->root_dir, resolved_value).lexically_normal().u8string();
            if (operation != "set" && resolved_value.empty())
                continue;
            const auto separator = separator_attr ? std::any_cast<const std::string&>(separator_attr->get()) : OPENDCC_PATH_LIST_SEPARATOR;
            const auto previous = get_env(name);
            auto result = resolved_value;
            if (!previous.empty() && !resolved_value.empty())
            {
                if (operation == "append")
                    result = previous + separator + resolved_value;
                else if (operation == "prepend")
                    result = resolved_value + separator + previous;
            }

            if (!set_env(name, result))
            {
                OPENDCC_WARN("Failed to set environment variable '{}' for package '{}'.", name, pkg_data->name);
                continue;
            }
        }
    }
}

void PackageLoader::update_python_path(const std::string& path, const std::string& operation, const std::string& pkg_name,
                                       const std::string& dependent_package)
{
    if (operation == "set")
        remove_package_python_paths();
    if (path.empty())
        return;
    PyLock lock;
    auto paths = module_::import("sys").attr("path").cast<list>();
    if (operation == "prepend")
        paths.attr("insert")(0, path);
    else if (!extend_python_path(path, pkg_name, dependent_package))
        return;
    m_python_paths.push_back({ path, operation == "prepend" });
}

void PackageLoader::remove_package_python_paths()
{
    PyLock lock;
    auto paths = module_::import("sys").attr("path").cast<list>();
    // Remove our additions in reverse order, leaving startup entries in place.
    for (auto entry = m_python_paths.rbegin(); entry != m_python_paths.rend(); ++entry)
    {
        const auto count = len(paths);
        for (size_t offset = 0; offset < count; ++offset)
        {
            const auto index = entry->prepended ? offset : count - offset - 1;
            const auto item = paths[index];
            if (isinstance<str>(item) && item.cast<std::string>() == entry->value)
            {
                paths.attr("pop")(index);
                break;
            }
        }
    }
    m_python_paths.clear();
}

void PackageLoader::apply_python_paths(const std::shared_ptr<PackageSharedData>& pkg_data, const std::string& dependent_package)
{
    const auto entries = find_package_attribute(pkg_data->resolved_attributes, "python.path");
    if (!entries)
        return;
    if (!std::any_cast<PackageAnyArray>(&entries->get()))
    {
        OPENDCC_WARN("Invalid Python paths for package '{}'.", pkg_data->name);
        return;
    }
    for (const auto& entry_value : std::any_cast<const PackageAnyArray&>(entries->get()))
    {
        const auto entry = std::any_cast<PackageAnyDictionary>(&entry_value);
        if (!entry)
        {
            OPENDCC_WARN("Invalid Python path entry for package '{}'.", pkg_data->name);
            continue;
        }
        const auto path = find_package_attribute(*entry, "path");
        const auto operation_attr = find_package_attribute(*entry, "operation");
        const auto path_attr = find_package_attribute(*entry, "is_path");
        const auto expand_attr = find_package_attribute(*entry, "expand_tokens");
        if (!path || !std::any_cast<std::string>(&path->get()) || (operation_attr && !std::any_cast<std::string>(&operation_attr->get())) ||
            (path_attr && !std::any_cast<bool>(&path_attr->get())) || (expand_attr && !std::any_cast<bool>(&expand_attr->get())))
        {
            OPENDCC_WARN("Invalid Python path entry for package '{}'.", pkg_data->name);
            continue;
        }
        const auto operation = operation_attr ? std::any_cast<const std::string&>(operation_attr->get()) : "append";
        if (operation != "set" && operation != "append" && operation != "prepend")
        {
            OPENDCC_WARN("Unknown Python path operation '{}' for package '{}'.", operation, pkg_data->name);
            continue;
        }
        auto value = std::any_cast<const std::string&>(path->get());
        if (!value.empty() && (!path_attr || std::any_cast<const bool&>(path_attr->get())))
            value = make_absolute_path(pkg_data->root_dir, value).lexically_normal().generic_u8string();
        update_python_path(value, operation, pkg_data->name, dependent_package);
    }
}

bool PackageLoader::unload(const std::string& pkg_name)
{
    auto data_it = m_pkg_shared_data.find(pkg_name);
    if (data_it == m_pkg_shared_data.end())
    {
        OPENDCC_ERROR("Failed to unload package '{}': package is unknown.", pkg_name);
        return false;
    }

    auto& data = data_it->second;
    if (!data->loaded)
    {
        return true;
    }

    // TODO: reconsider it, because on_uninitialize can be called on app shutdown
    // if (!data->get_resolved("base.unloadable", false))
    //{
    //    OPENDCC_WARN("Tried to unload a non-unloadable package '{}'", data->name);
    //    return false;
    //}

    // unload all dependees first
    auto deps = m_pkg_resolver->get_dependees(pkg_name);
    if (deps.empty())
    {
        OPENDCC_ERROR("Failed to unload package '{}'.", pkg_name);
        return false;
    }

    for (const auto& dep : deps)
    {
        auto it = m_pkg_shared_data.find(dep);
        if (it == m_pkg_shared_data.end())
        {
            OPENDCC_ERROR("Failed to unload package '{}': package data for '{}' is not found.", pkg_name, dep);
            return false;
        }
        const auto& pkg_data = it->second;

        // should we remove python paths before unload?
        const auto& first_entry_point = data->get_resolved("base.first_entry_point", "cpp");
        if (first_entry_point == "cpp")
        {
            // TODO
            // unload python modules
            unload_cpp_libs(pkg_data);
        }
        else if (first_entry_point == "python")
        {
            unload_cpp_libs(pkg_data);
            // TODO
            // unload python modules
        }
        pkg_data->loaded = false;
        pkg_data->loaded_entities.clear();
    }

    return true;
}

bool PackageLoader::load_python_modules(const std::shared_ptr<PackageSharedData>& pkg_data)
{
    static const std::string python_loads = "python.import";
    static const std::string python_entry_points = "python.entry_point";

    for (const auto& col : { python_loads, python_entry_points })
    {
        const auto& modules = pkg_data->get_resolved<PackageAnyArray>(col);
        for (const auto& value : modules)
        {
            const auto module = std::any_cast<PackageAnyDictionary>(&value);
            if (!module)
            {
                OPENDCC_WARN("Invalid '{}' entry for package '{}'.", col, pkg_data->name);
                continue;
            }
            auto it = module->find("module");
            if (it == module->end() || !std::any_cast<std::string>(&it->second))
            {
                OPENDCC_WARN("Failed to import module for package '{}': '{}' doesn't have 'module' entry or it is not a string.", pkg_data->name,
                             col);
                continue;
            }

            auto import_str = std::any_cast<const std::string&>(it->second);
            if (import_str.empty())
            {
                OPENDCC_WARN("Failed to import module for package '{}': 'module' defined but is empty.", pkg_data->name);
                continue;
            }

            PyLock lock;
            PackageEntryPoint* entry_point = nullptr;
            try
            {
                OPENDCC_INFO("Importing module '{}'...", import_str);
                auto module = module_::import(import_str.c_str());
                if (col == python_loads)
                {
                    continue;
                }

                auto module_dict = dict(module.attr("__dict__"));
                auto entry_point_py_class = module_::import("opendcc.packaging").attr("__dict__")["PackageEntryPoint"];
                auto entry_point_found = false;
                for (const auto& [k, v] : module_dict)
                {
                    const auto name = k.cast<std::string>();
                    if (!PyType_Check(v.ptr()) || v == entry_point_py_class)
                    {
                        continue;
                    }

                    if (PyObject_IsSubclass(v.ptr(), entry_point_py_class.ptr()) == 1)
                    {
                        entry_point_found = true;
                        auto entry_point_obj = v();
                        entry_point = entry_point_obj.cast<PackageEntryPoint*>();
                        entry_point->initialize(Package(pkg_data));

                        auto py_entry_point = std::make_unique<PythonEntryPoint>();
                        py_entry_point->entry_point_obj = entry_point_obj;
                        pkg_data->loaded_entities.emplace_back(std::move(py_entry_point));
                    }
                }

                if (!entry_point_found)
                {
                    OPENDCC_ERROR("Failed to find entry point in module '{}' of package '{}'.", import_str, pkg_data->name);
                }
            }
            catch (const pybind11::error_already_set& e)
            {
                OPENDCC_ERROR("Failed to import module '{}' for package '{}':\n{}", import_str, pkg_data->name, e.what());
                continue;
            }
        }
    }
    return true;
}

void PackageLoader::unload_cpp_libs(const std::shared_ptr<PackageSharedData>& pkg_data)
{
    for (auto it = pkg_data->loaded_entities.rbegin(); it != pkg_data->loaded_entities.rend(); ++it)
    {
        try
        {
            (*it)->uninitialize(Package(pkg_data));
        }
        catch (const std::exception& exception)
        {
            OPENDCC_ERROR("Exception occurred during entry point uninitialization in package '{}': {}.", pkg_data->name, exception.what());
            continue;
        }

        if (!(*it)->close())
        {
            OPENDCC_ERROR("Failed to close shared library loaded by package '{}'.", pkg_data->name);
            continue;
        }
    }
}

bool PackageLoader::extend_python_path(const std::string& path, const std::string& pkg_name, const std::string& dependent_package) const
{
    PyLock lock;
    try
    {
        module_::import("sys").attr("path").attr("append")(path);
#if PY_MAJOR_VERSION < 3
        std::function<void(const std::filesystem::path&)> traverse = [&path, &traverse](const std::filesystem::path& root) {
            for (const auto& dir_entry : std::filesystem::directory_iterator(root))
            {
                if (!dir_entry.is_directory())
                {
                    continue;
                }

                if (std::filesystem::exists(dir_entry.path() / "__init__.py"))
                {
                    const auto dir_path = dir_entry.path();
                    auto rel_path = std::filesystem::relative(dir_entry.path(), std::filesystem::u8path(path)).lexically_normal().generic_u8string();
                    std::replace(rel_path.begin(), rel_path.end(), '/', '.');
                    try
                    {
                        import(rel_path.c_str()).attr("__path__").attr("append")(dir_path.generic_u8string());
                        traverse(dir_entry.path());
                    }
                    catch (const error_already_set& err)
                    {
                        PyErr_Clear();
                    }
                }
            }
        };
        traverse(std::filesystem::u8path(path));
#endif
        return true;
    }
    catch (const pybind11::error_already_set& err)
    {
        OPENDCC_ERROR("Unable to load package '{}', required for '{}': failed to extend Python environment:\n{}", dependent_package, pkg_name,
                      err.what());
        return false;
    }
}

bool PackageLoader::load_cpp_libs(const std::shared_ptr<PackageSharedData>& pkg_data)
{
    static const std::string native_loads = "native.load";
    static const std::string native_entry_points = "native.entry_point";

    for (const auto& col : { native_loads, native_entry_points })
    {
        const auto& libs = pkg_data->get_resolved<PackageAnyArray>(col);
        for (const auto& value : libs)
        {
            const auto lib = std::any_cast<PackageAnyDictionary>(&value);
            if (!lib)
            {
                OPENDCC_WARN("Invalid '{}' entry for package '{}'.", col, pkg_data->name);
                continue;
            }
            auto it = lib->find("path");
            if (it == lib->end() || !std::any_cast<std::string>(&it->second))
            {
                OPENDCC_WARN("Failed to load shared library for package '{}': '{}' doesn't have 'path' entry or it is not a string.", pkg_data->name,
                             col);
                continue;
            }

            const auto path = make_absolute_path(pkg_data->root_dir, std::any_cast<const std::string&>(it->second));

            OPENDCC_INFO("Loading library '{}'...", path.u8string());
#ifdef OPENDCC_OS_WINDOWS
            if (auto handle = dl_open(path.u8string(), LOAD_WITH_ALTERED_SEARCH_PATH))
#else
            if (auto handle = dl_open(path.u8string(), RTLD_NOW))
#endif
            {
                if (col == native_entry_points)
                {
                    auto pkg_entry_point_fn = reinterpret_cast<PackageEntryPoint* (*)()>(dl_sym(handle, "opendcc_package_entry_point"));
                    if (!pkg_entry_point_fn)
                    {
                        OPENDCC_WARN(
                            "Failed to execute entry point declared in '{}': 'opendcc_package_entry_point' entry point is not defined, check usage of `OPENDCC_DEFINE_PACKAGE_ENTRY_POINT` macro.",
                            path.u8string());
                        dl_close(handle);
                        continue;
                    }

                    auto entry_point = pkg_entry_point_fn();
                    if (entry_point)
                    {
                        auto native_entry_point = std::make_unique<NativeEntryPoint>();
                        native_entry_point->entry_point = entry_point;
                        native_entry_point->handle = handle;
                        try
                        {
                            entry_point->initialize(Package(pkg_data));
                            pkg_data->loaded_entities.push_back(std::move(native_entry_point));
                        }
                        catch (const std::exception& exception)
                        {
                            OPENDCC_ERROR("Exception occurred during entry point initialization, defined in '{}': {}.", path.u8string(),
                                          exception.what());
                            native_entry_point->close();
                            continue;
                        }
                    }
                    else
                    {
                        OPENDCC_ERROR("Failed to create entry point declared in '{}': 'opendcc_package_entry_point' returned nullptr.",
                                      path.u8string());
                        dl_close(handle);
                        continue;
                    }
                }
                else
                {
                    auto native_library = std::make_unique<NativeLibrary>();
                    native_library->handle = handle;
                    pkg_data->loaded_entities.push_back(std::move(native_library));
                }
            }
            else
            {
                OPENDCC_ERROR("Failed to load library '{}' for package '{}': {}.", path.u8string(), pkg_data->name, dl_error_str());
                continue;
            }
        }
    }

    return true;
}

OPENDCC_NAMESPACE_CLOSE
