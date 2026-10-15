// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/undo/stack.h"
#include "opendcc/app/core/undo/router.h"

#include <pxr/pxr.h>
#include <pxr/base/tf/warning.h>

#include "opendcc/base/logging/logger.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::python::PythonCommandInterface;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;
using base::commands::UndoCommand;

using namespace commands;

UndoStack::UndoStack(size_t undo_limit /*= 100*/)
    : m_undo_limit(undo_limit)
{
    m_callback_handle = PythonCommandInterface::instance().register_event_callback(
        [this](std::shared_ptr<Command> cmd, const CommandArgs& args, const CommandResult& cmd_result) {
            auto py_str = PythonCommandInterface::generate_python_cmd_str(cmd, args);
            if (py_str.empty())
                py_str = cmd->get_command_name();

            std::string result_str;
            if (cmd_result.has_result())
            {
                const auto result_repr_str = PythonCommandInterface::generate_python_result_str(cmd_result);
                if (!result_repr_str.empty())
                    result_str = "Result: " + result_repr_str;
            }

            OPENDCC_INFO("Executing: \"{}\" {}", py_str, result_str);

            if (auto undo_cmd = std::dynamic_pointer_cast<UndoCommand>(cmd))
                push(CommandEntry { undo_cmd, py_str });
        });
}

void UndoStack::set_enabled(bool enabled)
{
    Lock lock(m_mutex);
    m_enabled = enabled;
    if (!enabled)
    {
        m_commands.clear();
        m_index = 0;
    }
}

bool UndoStack::is_enabled() const
{
    Lock lock(m_mutex);
    return m_enabled;
}

size_t UndoStack::get_undo_limit() const
{
    Lock lock(m_mutex);
    return m_undo_limit;
}

size_t UndoStack::get_size() const
{
    Lock lock(m_mutex);
    return m_commands.size();
}

bool UndoStack::can_undo() const
{
    Lock lock(m_mutex);
    return m_index != 0;
}

bool UndoStack::can_redo() const
{
    Lock lock(m_mutex);
    return m_index < m_commands.size();
}

void UndoStack::set_undo_limit(size_t limit)
{
    Lock lock(m_mutex);
    if (m_commands.size() > limit && limit != 0)
    {
        const auto delete_count = m_commands.size() - limit;
        m_commands.erase(m_commands.begin(), m_commands.begin() + delete_count);
        m_index = m_index > delete_count ? m_index - delete_count : 0ull;
    }
    m_undo_limit = limit;
}

void UndoStack::push(std::shared_ptr<UndoCommand> command, bool execute /*= false*/)
{
    Lock lock(m_mutex);
    if (!command)
        return;

    OPENDCC_INFO("Executing: \"{}\"", command->get_command_name());

    if (execute)
        command->redo();

    if (!m_enabled)
        return;

    push(CommandEntry { command, "" });
}

void UndoStack::undo()
{
    Lock lock(m_mutex);
    if (m_index == 0)
        return;

    const auto& command = m_commands[m_index - 1];
    if (command.log_string.empty())
        OPENDCC_INFO("Undo: \"{}\"", command.cmd->get_command_name());
    else
        OPENDCC_INFO("Undo: \"{}\"", command.log_string);

    command.cmd->undo();
    --m_index;
}

void UndoStack::redo()
{
    Lock lock(m_mutex);
    if (m_index == m_commands.size())
        return;

    const auto& command = m_commands[m_index];
    if (command.log_string.empty())
        OPENDCC_INFO("Redo: \"{}\"", command.cmd->get_command_name());
    else
        OPENDCC_INFO("Redo: \"{}\"", command.log_string);

    command.cmd->redo();
    ++m_index;
}

void UndoStack::clear()
{
    Lock lock(m_mutex);
    m_commands.clear();
    m_index = 0;
}

UndoStack::~UndoStack()
{
    PythonCommandInterface::instance().unregister_event_callback(m_callback_handle);
}

void UndoStack::push(const CommandEntry& command_entry)
{
    Lock lock(m_mutex);
    if (!command_entry.cmd || !m_enabled)
        return;

    auto last_command = m_commands.empty() ? nullptr : m_commands.back().cmd.get();
    const bool try_merge = last_command != nullptr && last_command->get_command_name() == command_entry.cmd->get_command_name();
    if (try_merge && last_command->merge_with(command_entry.cmd.get()))
    {
        return;
    }

    m_commands.erase(m_commands.begin() + m_index, m_commands.end());

    if (m_undo_limit == 0 || m_commands.size() != m_undo_limit)
    {
        m_index++;
    }
    else
    {
        m_commands.pop_front();
    }
    m_commands.push_back(command_entry);
}

OPENDCC_NAMESPACE_CLOSE
