// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <opendcc/base/app_config/config.h>
#include "opendcc/base/logging/logger.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.base.app_config");

ApplicationConfig::ApplicationConfig(const std::string& filename)
{
    try
    {
        m_config = cpptoml::parse_file(filename);
    }
    catch (cpptoml::parse_exception e)
    {
        OPENDCC_ERROR(e.what());
    }
}

bool ApplicationConfig::is_valid() const
{
    return m_config != nullptr;
}

OPENDCC_NAMESPACE_CLOSE
