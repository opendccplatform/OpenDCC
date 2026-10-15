/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/ipc_commands_api/api.h"
#include "opendcc/base/ipc_commands_api/command.h"
#include "opendcc/base/ipc_commands_api/server_info.h"

#include <memory>

OPENDCC_NAMESPACE_OPEN

namespace base::ipc
{

    /**
     * @class CommandServerImpl
     * @brief Private implementation class for CommandServer.
     */
    class CommandServerImpl;

    /**
     * @class CommandServer
     * @brief The CommandServer is designed to listen to the address "tcp://info.hostname:info.input_port".
     * Upon receiving a command, the server makes an attempt of handling it by utilizing the "CommandRegistry".
     * The CommandServer class also provides functionality for sending commands over ipc.
     * It uses the CommandServerImpl class for the actual implementation.
     */
    class OPENDCC_BASE_IPC_COMMANDS_API CommandServer
    {
    public:
        /**
         * @brief Constructs a CommandServer with the given server information.
         * @param info The server information.
         * @note If the ``info.input_port`` value in the ServerInfo is set to ``ServerInfo::s_invalid_port``,
         * the CommandServer will attempt to use the first available TCP port.
         */
        CommandServer(const ServerInfo& info);
        ~CommandServer();

        /**
         * @brief Sends a command string over ipc to the server with the given server information.
         * @param info The server information.
         * @param command The command string to send.
         * @note Does nothing if server is not reachable.
         */
        void send_command(const ServerInfo& info, const Command& command);

        /**
         * @brief Sends a command string over ipc to the process with the given PID.
         * @param pid The process ID.
         * @param command The command string to send.
         * @note Does nothing if server is not reachable.
         * @note The CommandServer will attempt to locate the port associated with the specified process ID in the ServerRegistry.
         * If the port cannot be found, the CommandServer takes no action.
         */
        void send_command(const std::string& pid, const Command& command);

        /**
         * @brief Returns the server information.
         * @return The server information.
         */
        const ServerInfo& get_info() const;

        /**
         * @brief Checks if the CommandServer object is valid.
         * @return True if the object is valid, false otherwise.
         */
        bool valid() const;

        static void set_server_timeout(const int server_timeout);

        static int get_server_timeout();

    private:
        static int s_server_timeout;

        std::unique_ptr<CommandServerImpl> m_impl;
    };

}

OPENDCC_NAMESPACE_CLOSE
