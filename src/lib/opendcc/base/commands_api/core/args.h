/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/api.h"
#include "opendcc/base/commands_api/core/command_syntax.h"

#include <memory>
#include <typeindex>
#include <vector>
#include <unordered_map>

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    class OPENDCC_BASE_COMMANDS_API CommandArgBase
    {
    public:
        virtual ~CommandArgBase() = default;
        virtual const std::type_info& get_type_info() const = 0;
        bool is_convertible(const std::shared_ptr<CommandArgBase>& arg) const;
        bool is_convertible(const std::type_index& arg_type) const;
        bool is_convertible(const TypeIndices& arg_types) const;
    };

    template <class T>
    class CommandArg : public CommandArgBase
    {
    public:
        CommandArg(const T& value)
            : m_value(value)
        {
        }

        const std::type_info& get_type_info() const override { return typeid(T); }

        operator T() const { return m_value; }

        const T& get_value() const { return m_value; }

    private:
        T m_value;
    };

    class OPENDCC_BASE_COMMANDS_API CommandArgs
    {
    public:
        CommandArgs() = default;
        CommandArgs(const CommandArgs&) = default;
        CommandArgs(CommandArgs&&) = default;
        CommandArgs& operator=(const CommandArgs&) = default;
        CommandArgs& operator=(CommandArgs&&) = default;

        CommandArgs& arg(std::shared_ptr<CommandArgBase> arg);
        CommandArgs& kwarg(std::string name, std::shared_ptr<CommandArgBase> arg);
        CommandArgs& pos_arg(uint32_t pos, std::shared_ptr<CommandArgBase> arg);

        template <class T>
        CommandArgs& arg(T&& arg);
        template <class T>
        CommandArgs& kwarg(std::string name, T&& arg);
        template <class T>
        CommandArgs& pos_arg(uint32_t pos, T&& arg);

        std::shared_ptr<CommandArgBase> get_arg(uint32_t pos) const;
        std::shared_ptr<CommandArgBase> get_kwarg(std::string name) const;

        template <class T>
        std::shared_ptr<CommandArg<T>> get_arg(uint32_t pos) const
        {
            if (auto arg = get_arg(pos))
                return std::dynamic_pointer_cast<CommandArg<T>>(arg);
            return nullptr;
        }

        template <class T>
        std::shared_ptr<CommandArg<T>> get_kwarg(std::string name) const
        {
            if (auto arg = get_kwarg(name))
                return std::dynamic_pointer_cast<CommandArg<T>>(arg);
            return nullptr;
        }

        bool has_arg(uint32_t pos) const;
        bool has_kwarg(std::string name) const;

        const std::vector<std::shared_ptr<CommandArgBase>>& get_args() const;
        const std::unordered_map<std::string, std::shared_ptr<CommandArgBase>>& get_kwargs() const;

    private:
        std::vector<std::shared_ptr<CommandArgBase>> m_args;
        std::unordered_map<std::string, std::shared_ptr<CommandArgBase>> m_kwargs;
    };

    template <class T>
    struct ArgTypeInfo
    {
        using Type = T;
    };

    template <size_t N>
    struct ArgTypeInfo<char[N]>
    {
        using Type = std::string;
    };

    template <>
    struct ArgTypeInfo<char*>
    {
        using Type = std::string;
    };

    template <>
    struct ArgTypeInfo<const char*>
    {
        using Type = std::string;
    };

    template <class T>
    CommandArgs& CommandArgs::arg(T&& arg)
    {
        m_args.push_back(std::make_shared<CommandArg<typename ArgTypeInfo<std::decay_t<T>>::Type>>(std::forward<T>(arg)));
        return *this;
    }

    template <class T>
    CommandArgs& CommandArgs::kwarg(std::string name, T&& arg)
    {
        m_kwargs[name] = std::make_shared<CommandArg<typename ArgTypeInfo<std::decay_t<T>>::Type>>(std::forward<T>(arg));
        return *this;
    }

    template <class T>
    CommandArgs& CommandArgs::pos_arg(uint32_t pos, T&& arg)
    {
        m_args.resize(std::max(m_args.size(), static_cast<size_t>(pos + 1)));
        m_args[pos] = std::make_shared<CommandArg<typename ArgTypeInfo<std::decay_t<T>>::Type>>(std::forward<T>(arg));
        return *this;
    }

}

OPENDCC_NAMESPACE_CLOSE
