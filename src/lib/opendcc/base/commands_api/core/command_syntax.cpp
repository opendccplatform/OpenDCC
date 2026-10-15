// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/core/command_syntax.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    namespace
    {
        std::string make_valid_arg_name(const std::string& name)
        {
            if (name.empty())
                return "_";
            std::string result;
            result.reserve(name.size());

            auto it = name.begin();
            if (('a' <= *it && *it <= 'z') || ('A' <= *it && *it <= 'Z') || *it == '_')
                result.push_back(*it);
            else
                result.push_back('_');

            for (++it; it != name.end(); ++it)
            {
                if (('a' <= *it && *it <= 'z') || ('A' <= *it && *it <= 'Z') || ('0' <= *it && *it <= '9') || *it == '_')
                    result.push_back(*it);
                else
                    result.push_back('_');
            }
            return result;
        }
    };

    CommandSyntax::CommandSyntax() {}

    CommandSyntax& CommandSyntax::arg(const std::string& name, const TypeIndices& arg_types, const std::string& description /*= ""*/)
    {
        const auto valid_name = make_valid_arg_name(name);
        if (!has_arg(valid_name))
            m_arg_descriptors.emplace_back(valid_name, arg_types, description);
        return *this;
    }

    CommandSyntax& CommandSyntax::kwarg(const std::string& name, const TypeIndices& arg_types, const std::string& description /*= ""*/)
    {
        const auto valid_name = make_valid_arg_name(name);
        if (!has_arg(valid_name))
            m_kwarg_descriptors.emplace(valid_name, ArgDescriptor(valid_name, arg_types, description));
        return *this;
    }

    CommandSyntax& CommandSyntax::result(std::type_index result_type, const std::string& description /*= ""*/)
    {
        m_result_descriptor = ArgDescriptor("", TypeIndices { result_type }, description);
        return *this;
    }

    CommandSyntax& CommandSyntax::description(const std::string& description)
    {
        m_cmd_description = description;
        return *this;
    }

    const CommandSyntax::ArgDescriptor& CommandSyntax::get_arg_descriptor(uint32_t pos) const
    {
        static ArgDescriptor empty;
        if (pos >= m_arg_descriptors.size())
            return empty;

        return m_arg_descriptors[pos];
    }

    const CommandSyntax::ArgDescriptor& CommandSyntax::get_kwarg_descriptor(const std::string& name) const
    {
        static ArgDescriptor empty;
        auto it = m_kwarg_descriptors.find(name);
        if (it != m_kwarg_descriptors.end())
            return it->second;
        else
            return empty;
    }

    const CommandSyntax::ArgDescriptor& CommandSyntax::get_result_descriptor() const
    {
        return m_result_descriptor;
    }

    bool CommandSyntax::has_arg(const std::string& name) const
    {
        for (const auto& descr : m_arg_descriptors)
        {
            if (descr.name == name)
                return true;
        }
        return m_kwarg_descriptors.find(name) != m_kwarg_descriptors.end();
    }

    const std::vector<CommandSyntax::ArgDescriptor>& CommandSyntax::get_arg_descriptors() const
    {
        return m_arg_descriptors;
    }

    const std::string& CommandSyntax::get_command_description() const
    {
        return m_cmd_description;
    }

    const std::unordered_map<std::string, CommandSyntax::ArgDescriptor>& CommandSyntax::get_kwarg_descriptors() const
    {
        return m_kwarg_descriptors;
    }

}

OPENDCC_NAMESPACE_CLOSE
