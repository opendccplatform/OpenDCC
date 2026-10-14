// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/ipc_commands_api/server_registry.h"
#include "opendcc/base/ipc_commands_api/server.h"

#include "opendcc/base/vendor/cppzmq/zmq.hpp"
#include "opendcc/base/logging/logger.h"
#include <future>
#include <iostream>

OPENDCC_NAMESPACE_OPEN

OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.base.ipc");

IPC_NAMESPACE_OPEN

//////////////////////////////////////////////////////////////////////////
// ServerRegistry
//////////////////////////////////////////////////////////////////////////

/* static */
ServerRegistry& ServerRegistry::instance()
{
    static ServerRegistry s_instance;
    return s_instance;
}

void ServerRegistry::add_server(const std::string& pid, const ServerInfo& info)
{
    m_servers[pid] = info;
}

ServerInfo ServerRegistry::find_server(const std::string& pid)
{
    const auto find = m_servers.find(pid);

    if (find == m_servers.end())
    {
        return {};
    }

    return find->second;
}

ServerRegistry::ServerRegistry() {}

ServerRegistry::~ServerRegistry() {}

IPC_NAMESPACE_CLOSE

OPENDCC_NAMESPACE_CLOSE
