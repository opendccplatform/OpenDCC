/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/ipc_commands_api/api.h"

#include <string>

OPENDCC_NAMESPACE_OPEN

namespace base::ipc
{

    /**
     * @brief A struct that holds information about a server.
     */
    struct OPENDCC_BASE_IPC_COMMANDS_API ServerInfo
    {
        /**
         * @brief A constant representing an invalid port number.
         */
        static const uint32_t s_invalid_port = -1;

        /**
         * @brief The hostname of the ipc server.
         */
        std::string hostname;

        /**
         * @brief The input port number of the ipc server.
         */
        uint32_t input_port = s_invalid_port;

        /**
         * @brief Creates a ServerInfo object from a string representation of the TCP address.
         * @param tcp_address The TCP address in the format "tcp://hostname:port".
         */
        static ServerInfo from_string(const std::string& tcp_address);

        /**
         * @brief Checks if the ServerInfo object is valid.
         * @return true if the object is valid, false otherwise.
         */
        bool valid() const;

        /**
         * @brief Checks if the ServerInfo object is online.
         * @return true if the object is online, false otherwise.
         */
        bool is_online() const;

        /**
         * @brief Get the TCP address.
         * @return The TCP address as a string.
         */
        std::string get_tcp_address() const;
    };

}

OPENDCC_NAMESPACE_CLOSE
