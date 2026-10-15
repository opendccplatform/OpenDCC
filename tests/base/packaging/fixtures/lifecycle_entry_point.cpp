/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/packaging/package_entry_point.h"
#include "opendcc/base/utils/env.h"
#include <fstream>
#include <string>
#include <stdexcept>

OPENDCC_NAMESPACE_OPEN

using base::packaging::PackageEntryPoint;
using base::packaging::Package;

class LifecycleEntryPoint : public PackageEntryPoint
{
public:
    ~LifecycleEntryPoint() override
    {
        if (!m_filename.empty())
            std::ofstream(m_filename, std::ios::app) << "native:destroy:" << m_package_name << '\n';
    }
    void initialize(const Package& package) override
    {
        m_filename = std::any_cast<std::string>(package.get_attribute("test.event_log"));
        m_package_name = package.get_name();
        record("native:init", package);
        const auto environment_variable = package.get_attribute("test.environment_variable");
        if ((std::any_cast<std::string>(&environment_variable) != nullptr))
            std::ofstream(m_filename, std::ios::app)
                << "native:environment:" << base::utils::get_env(std::any_cast<std::string>(environment_variable)) << '\n';
        const auto fail = package.get_attribute("test.fail_initialize");
        if ((std::any_cast<bool>(&fail) != nullptr) && std::any_cast<bool>(fail))
            throw std::runtime_error("Native test initialization failed");
    }
    void uninitialize(const Package& package) override { record("native:uninit", package); }

private:
    std::string m_filename;
    std::string m_package_name;
    void record(const char* event, const Package& package)
    {
        const auto filename = package.get_attribute("test.event_log");
        if ((std::any_cast<std::string>(&filename) != nullptr))
            std::ofstream(std::any_cast<std::string>(filename), std::ios::app) << event << ':' << package.get_name() << '\n';
    }
};

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(LifecycleEntryPoint);

OPENDCC_NAMESPACE_CLOSE
