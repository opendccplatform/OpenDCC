// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/core/args.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{
    const std::vector<std::shared_ptr<CommandArgBase>>& CommandArgs::get_args() const
    {
        return m_args;
    }

    const std::unordered_map<std::string, std::shared_ptr<CommandArgBase>>& CommandArgs::get_kwargs() const
    {
        return m_kwargs;
    }

    CommandArgs& CommandArgs::kwarg(std::string name, std::shared_ptr<CommandArgBase> arg)
    {
        m_kwargs[name] = arg;
        return *this;
    }

    CommandArgs& CommandArgs::pos_arg(uint32_t pos, std::shared_ptr<CommandArgBase> arg)
    {
        m_args.resize(std::max(m_args.size(), static_cast<size_t>(pos + 1)));
        m_args[pos] = arg;
        return *this;
    }

    CommandArgs& CommandArgs::arg(std::shared_ptr<CommandArgBase> arg)
    {
        m_args.push_back(arg);
        return *this;
    }

    std::shared_ptr<CommandArgBase> CommandArgs::get_arg(uint32_t pos) const
    {
        if (pos >= m_args.size())
            return nullptr;
        return m_args[pos];
    }

    std::shared_ptr<CommandArgBase> CommandArgs::get_kwarg(std::string name) const
    {
        auto iter = m_kwargs.find(name);
        if (iter == m_kwargs.end())
            return nullptr;
        return iter->second;
    }

    bool CommandArgs::has_arg(uint32_t pos) const
    {
        return m_args.size() > pos;
    }

    bool CommandArgs::has_kwarg(std::string name) const
    {
        return m_kwargs.find(name) != m_kwargs.end();
    }

    bool CommandArgBase::is_convertible(const std::shared_ptr<CommandArgBase>& arg) const
    {
        return CommandRegistry::is_convertible(get_type_info(), arg->get_type_info());
    }

    bool CommandArgBase::is_convertible(const std::type_index& arg_type) const
    {
        return CommandRegistry::is_convertible(get_type_info(), arg_type);
    }

    bool CommandArgBase::is_convertible(const TypeIndices& arg_types) const
    {
        for (const auto& type : arg_types)
        {
            if (is_convertible(type))
                return true;
        }
        return false;
    }

}

OPENDCC_NAMESPACE_CLOSE
