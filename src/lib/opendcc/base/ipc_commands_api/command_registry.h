/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/ipc_commands_api/api.h"
#include "opendcc/base/ipc_commands_api/command.h"

#include <string>
#include <functional>
#include <unordered_map>

OPENDCC_NAMESPACE_OPEN

namespace base::ipc
{

    /**
     * @brief The CommandRegistry class for registering and handling ipc commands.
     */
    class OPENDCC_BASE_IPC_COMMANDS_API CommandRegistry
    {
    public:
        CommandRegistry(const CommandRegistry&) = delete;
        CommandRegistry(CommandRegistry&&) = delete;
        CommandRegistry& operator=(const CommandRegistry&) = delete;
        CommandRegistry& operator=(CommandRegistry&&) = delete;

        /**
         * @brief A type alias for a command handler function.
         */
        using CommandHandler = std::function<void(const Command&)>;

        /**
         * @brief Returns the singleton instance of the CommandRegistry.
         * @return A reference to the singleton instance of the CommandRegistry.
         */
        static CommandRegistry& instance();

        /**
         * @brief Adds a command handler for the specified command name.
         * @param name The name of the ipc command.
         * @param handler The command handler function.
         */
        void add_handler(const std::string& name, const CommandHandler& handler);

        /**
         * @brief Handles the specified command by invoking the appropriate command handler.
         * @param command The command to handle.
         */
        void handle_command(const Command& command);

    private:
        CommandRegistry();
        ~CommandRegistry();

        std::unordered_map<std::string /*name*/, CommandHandler> m_handlers;
    };

}

OPENDCC_NAMESPACE_CLOSE
