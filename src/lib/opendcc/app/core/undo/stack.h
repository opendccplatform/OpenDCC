/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/export.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/python_bindings/python_command_interface.h"

#include <deque>
#include <memory>
#include <mutex>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    class OPENDCC_API UndoStack
    {
    public:
        UndoStack(size_t undo_limit = 100);
        UndoStack(const UndoStack&) = delete;
        UndoStack(UndoStack&&) = delete;
        UndoStack& operator=(const UndoStack&) = delete;
        UndoStack& operator=(UndoStack&&) = delete;

        ~UndoStack();

        void set_enabled(bool enabled);
        bool is_enabled() const;
        size_t get_undo_limit() const;
        size_t get_size() const;
        bool can_undo() const;
        bool can_redo() const;
        void set_undo_limit(size_t limit);

        void push(std::shared_ptr<base::commands::UndoCommand> command, bool execute = false);
        void undo();
        void redo();
        void clear();

    private:
        struct CommandEntry
        {
            std::shared_ptr<base::commands::UndoCommand> cmd;
            std::string log_string;
        };

        void push(const CommandEntry& command_entry);

        using Lock = std::lock_guard<std::recursive_mutex>;

        size_t m_undo_limit = 0;
        size_t m_index = 0;
        mutable std::recursive_mutex m_mutex;
        std::deque<CommandEntry> m_commands;
        base::commands::python::PythonCommandInterface::EventDispatcherHandle m_callback_handle;
        bool m_enabled = true;
    };
}

OPENDCC_NAMESPACE_CLOSE
