// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/package_loader.h"
#include "opendcc/base/vendor/ghc/filesystem.hpp"
#include "opendcc/base/logging/logger.h"
#include "opendcc/base/pybind_bridge/pybind11.h"
#if defined(OPENDCC_OS_WINDOWS)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
PXR_NAMESPACE_USING_DIRECTIVE
#include "pxr/base/vt/array.h"

#include "opendcc/base/utils/library.h"
#include "package_entry_point.h"
#include "opendcc/base/utils/string_utils.h"
#include "opendcc/base/utils/env.h"
#include "opendcc/base/utils/file_system.h"

using namespace pybind11;
OPENDCC_NAMESPACE_OPEN

namespace
{
    ghc::filesystem::path make_absolute_path(const std::string& root, const std::string& path)
    {
        auto result = ghc::filesystem::path(path);
        if (result.is_relative())
        {
            result = root / result;
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

PackageLoader::PackageLoader(std::shared_ptr<PackageResolver> pkg_resolver,
                             std::unordered_map<std::string, std::shared_ptr<PackageSharedData>>& pkg_shared_data)
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

        const auto& environment = pkg_data->get_resolved<VtDictionary>("environment");
        if (!environment.empty())
        {
            for (const auto& attr : environment)
            {
                const auto& env_name = attr.first;
                // PYTHONPATH is handled in different way below
                if (env_name == "PYTHONPATH")
                {
                    continue;
                }

                const auto& vals = attr.second.Get<VtArray<VtDictionary>>();
                // TODO: revisit it, make more clever design with append/prepend to variable, etc
                // for now support only for PYTHONPATH and PATH. Prepend new values for PATH
                if (env_name != "PATH")
                {
                    continue;
                }

                auto cur_env_var_value = get_env(env_name);
                for (const auto& val : vals)
                {
                    const auto env_val = val.GetValueAtPath("value");
                    if (!env_val)
                    {
                        OPENDCC_WARN("Failed to extend environment variable '{}' for package '{}': 'value' entry not found.", env_name, dep);
                        continue;
                    }

                    const auto path = make_absolute_path(pkg_data->root_dir, env_val->Get<std::string>()).string();
                    cur_env_var_value = path + OPENDCC_PATH_LIST_SEPARATOR + cur_env_var_value;
                }
                set_env(env_name, cur_env_var_value);
            }
        }

        const auto& pythonpath_env = pkg_data->get_resolved<VtArray<VtDictionary>>("environment.PYTHONPATH");
        for (const auto& env_entry : pythonpath_env)
        {
            const auto env_val = env_entry.GetValueAtPath("value");
            if (!env_val)
            {
                OPENDCC_WARN("Failed to extend Python environment for package '{}': 'value' entry not found.", dep);
                continue;
            }
            auto path = make_absolute_path(pkg_data->root_dir, env_val->Get<std::string>());
            if (!extend_python_path(path.lexically_normal().generic_string(), pkg_name, pkg_data->name))
            {
                continue;
            }
        }

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
        const auto& modules = pkg_data->get_resolved<VtArray<VtDictionary>>(col);
        for (const auto& module : modules)
        {
            auto it = module.find("module");
            if (it == module.end() || !it->second.IsHolding<std::string>())
            {
                OPENDCC_WARN("Failed to import module for package '{}': '{}' doesn't have 'module' entry or it is not a string.", pkg_data->name,
                             col);
                continue;
            }

            auto import_str = it->second.UncheckedGet<std::string>();
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
        std::function<void(const ghc::filesystem::path&)> traverse = [&path, &traverse](const ghc::filesystem::path& root) {
            for (const auto& dir_entry : ghc::filesystem::directory_iterator(root))
            {
                if (!dir_entry.is_directory())
                {
                    continue;
                }

                if (ghc::filesystem::exists(dir_entry / "__init__.py"))
                {
                    const auto dir_path = dir_entry.path();
                    auto rel_path = ghc::filesystem::relative(dir_entry, path).lexically_normal().generic_string();
                    std::replace(rel_path.begin(), rel_path.end(), '/', '.');
                    try
                    {
                        import(rel_path.c_str()).attr("__path__").attr("append")(dir_path.generic_string());
                        traverse(dir_entry);
                    }
                    catch (const error_already_set& err)
                    {
                        PyErr_Clear();
                    }
                }
            }
        };
        traverse(path);
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
        const auto& libs = pkg_data->get_resolved<VtArray<VtDictionary>>(col);
        for (const auto& lib : libs)
        {
            auto it = lib.find("path");
            if (it == lib.end() || !it->second.IsHolding<std::string>())
            {
                OPENDCC_WARN("Failed to load shared library for package '{}': '{}' doesn't have 'path' entry or it is not a string.", pkg_data->name,
                             col);
                continue;
            }

            const auto path = make_absolute_path(pkg_data->root_dir, it->second.Get<std::string>());

            OPENDCC_INFO("Loading library '{}'...", path.string());
#ifdef OPENDCC_OS_WINDOWS
            if (auto handle = dl_open(path.string(), LOAD_WITH_ALTERED_SEARCH_PATH))
#else
            if (auto handle = dl_open(path.string(), RTLD_NOW))
#endif
            {
                if (col == native_entry_points)
                {
                    auto pkg_entry_point_fn = reinterpret_cast<PackageEntryPoint* (*)()>(dl_sym(handle, "opendcc_package_entry_point"));
                    if (!pkg_entry_point_fn)
                    {
                        OPENDCC_WARN(
                            "Failed to execute entry point declared in '{}': 'opendcc_package_entry_point' entry point is not defined, check usage of `OPENDCC_DEFINE_PACKAGE_ENTRY_POINT` macro.",
                            path.string());
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
                            OPENDCC_ERROR("Exception occurred during entry point initialization, defined in '{}': {}.", path.string(),
                                          exception.what());
                            native_entry_point->close();
                            continue;
                        }
                    }
                    else
                    {
                        OPENDCC_ERROR("Failed to create entry point declared in '{}': 'opendcc_package_entry_point' returned nullptr.",
                                      path.string());
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
                OPENDCC_ERROR("Failed to load library '{}' for package '{}': {}.", path.string(), pkg_data->name, dl_error_str());
                continue;
            }
        }
    }

    return true;
}

OPENDCC_NAMESPACE_CLOSE
