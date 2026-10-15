// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/ipc_commands_api/server_info.h"

#include "opendcc/base/utils/string_utils.h"

OPENDCC_NAMESPACE_OPEN

namespace base::ipc
{

    static std::string make_tcp_address(const std::string& hostname, const uint32_t port)
    {
        const auto port_str = port == ServerInfo::s_invalid_port ? "*" : std::to_string(port);
        return "tcp://" + hostname + ":" + port_str;
    }

    static const auto tcp_port_max = 65'535;

    /* static */
    ServerInfo ServerInfo::from_string(const std::string& tcp_address)
    {
        ServerInfo result;

        // tcp_address must be:
        // tcp://[0-255].[0-255].[0-255].[0-255]:[0-65'535]
        // or
        // tcp://url:[0-65'535]
        const auto split = utils::split(tcp_address, ':');

        if (split.size() != 3)
        {
            return result;
        }

        if (split[0] != "tcp")
        {
            return result;
        }

        uint32_t port;
        try
        {
            port = std::stoi(split[2]);
        }
        catch (...)
        {
            return result;
        }

        auto hostname = split[1];
        if (hostname.size() <= 2 || hostname[0] != '/' || hostname[1] != '/')
        {
            return result;
        }

        hostname = std::string(hostname.begin() + 2, hostname.end());

        result.input_port = port;
        result.hostname = hostname;

        return result;
    }

    bool ServerInfo::valid() const
    {
        const auto empty = hostname.empty();

        if (input_port == s_invalid_port)
        {
            return !empty;
        }

        if (input_port < 0 || input_port > tcp_port_max)
        {
            return false;
        }

        return !empty;
    }

    std::string ServerInfo::get_tcp_address() const
    {
        return make_tcp_address(hostname, input_port);
    }

}

OPENDCC_NAMESPACE_CLOSE
