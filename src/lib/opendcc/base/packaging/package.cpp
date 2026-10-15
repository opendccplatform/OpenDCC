// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/package.h"
#include "opendcc/base/packaging/core_properties.h"

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    const std::string& Package::get_name() const
    {
        return m_data->name;
    }

    const PackageAnyDictionary& Package::get_all_attributes() const
    {
        return m_data->resolved_attributes;
    }

    Package::Package(const std::shared_ptr<PackageSharedData>& data)
        : m_data(data)
    {
    }

    const std::any& Package::get_attribute(const std::string& name) const
    {
        auto it = find_package_attribute(m_data->resolved_attributes, name);
        if (!it)
        {
            static const std::any empty;
            return empty;
        }
        return it->get();
    }

    bool Package::is_loaded() const
    {
        return m_data->loaded;
    }

    const PackageAnyDictionary& Package::get_direct_dependencies() const
    {
        return m_data->direct_dependencies;
    }

    const std::string& Package::get_root_dir() const
    {
        return m_data->root_dir;
    }

    bool Package::is_valid() const
    {
        return m_data != nullptr;
    }

}

OPENDCC_NAMESPACE_CLOSE
